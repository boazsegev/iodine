#include "iodine_websocket.h"
#include <ruby/io.h>
#include <arpa/inet.h>

/* ////////////////////////////////////////////////////////////
This file creates an HTTP server based on the Iodine libraries.

The server is (mostly) Rack compatible, except:

1. upgrade requests are handled using special upgrade handlers.
2. if a String is returned, it is a assumed to be a status 200 Html data?

//////////////////////////////////////////////////////////// */

//////////////
// general global definitions we will use herein.
static VALUE rWebsocket;           // The Iodine::Http::Websocket class
static rb_encoding* UTF8Encoding;  // encoding object
static int UTF8EncodingIndex;
static ID server_var_id;        // id for the Server variable (pointer)
static ID fd_var_id;            // id for the file descriptor (Fixnum)
static ID call_proc_id;         // id for `#call`
static ID dup_func_id;          // id for the buffer.dup method
static ID new_func_id;          // id for the Class.new method
static ID on_open_func_id;      // the on_open callback's ID
static ID on_close_func_id;     // the on_close callback's ID
static ID on_shutdown_func_id;  // a callback's ID
static ID on_msg_func_id;       // a callback's ID
static char ws_service_name[] = "websocket";

/////////////////////////////////
// Parsing types

// The Protocol Object
struct WebsocketProtocol {
  struct Protocol protocol;
  VALUE handler;
  VALUE buffer;
  struct {
    union {
      unsigned len1 : 16;
      unsigned long len2 : 64;
      char bytes[8];
    } psize;
    size_t length;
    size_t received;
    char mask[4];
    char tmp_buffer[1024];
    struct {
      unsigned op_code : 4;
      unsigned rsv3 : 1;
      unsigned rsv2 : 1;
      unsigned rsv1 : 1;
      unsigned fin : 1;
    } head, head2;
    struct {
      unsigned size : 7;
      unsigned masked : 1;
    } sdata;
    struct {
      unsigned has_mask : 1;
      unsigned at_mask : 2;
      unsigned has_len : 1;
      unsigned at_len : 3;
      unsigned rsv : 1;
    } state;
  } parser;
};

// The protocol's destructor
static void WebsocketProtocol_destroy(struct WebsocketProtocol* ws) {
  if (ws->handler)
    Registry.remove(ws->handler);
  if (ws->buffer)
    Registry.remove(ws->buffer);
  free(ws);
}

// network byte issues
#ifdef __BIG_ENDIAN__
#define IS_BIG_ENDIAN 1
#else
#define IS_BIG_ENDIAN (!*(unsigned char*)&(uint16_t){1})
#endif

//////////////////////////////////////
// GVL lock for ruby API calls

static void* resize_buffer_in_gvl(void* data) {
  struct WebsocketProtocol* ws = data;
  rb_str_resize(ws->buffer, RSTRING_LEN(ws->buffer) + ws->parser.length -
                                ws->parser.received);
  return 0;
}

static void* on_message_in_gvl(void* data) {
  struct WebsocketProtocol* ws = data;
  // This will create a copy of heach message - can we trust the user not to
  // save the string for later use? We'll have to, for most common usecase is
  // JSON... why have another copy of the same data?
  //            VALUE str = rb_obj_dup(ws->buffer);
  //            RubyCaller.call2(ws->handler, on_msg_func_id, 1, &str);
  // vs.
  //            RubyCaller.call2(ws->handler, on_msg_func_id, 1, &ws->buffer);
  RubyCaller.call2(ws->handler, on_msg_func_id, 1, &ws->buffer);
  // make sure the string is modifiable
  rb_str_modify(ws->buffer);
  // reset the buffer
  rb_str_set_len(ws->buffer, 0);
  rb_enc_associate(ws->buffer, BinaryEncoding);
  return 0;
}

//////////////////////////////////////
// Protocol Helper Methods

// performs a block of code on an active connection.
// initiated by each_block
static void each_protocol_block(struct Server* srv, int fd, void* task) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(srv, fd);
  if (ws && ws->protocol.service == ws_service_name)
    RubyCaller.call2((VALUE)task, call_proc_id, 1, &(ws->handler));
}

// performs pending protocol task while managing it's Ruby registry.
// initiated by each
static void each_protocol_async(struct Server* srv, int fd, void* task) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(srv, fd);
  if (ws && ws->protocol.service == ws_service_name)
    RubyCaller.call2((VALUE)task, call_proc_id, 1, &(ws->handler));
  // the task should be removed even if the handler was closed or doesn't exist,
  // since it was scheduled and we need to clear every reference.
  Registry.remove((VALUE)task);
}

static void each_protocol_async_schedule(struct Server* srv,
                                         int fd,
                                         void* task) {
  // Registry is a bag, adding the same task multiple times will increase the
  // number of references we have.
  Registry.add((VALUE)task);
  Server.fd_task(srv, fd, each_protocol_async, task);
}

static void websocket_close(server_pt srv, int fd) {
  Server.write(srv, fd, "\x88\x00", 2);
  Server.close(srv, fd);
  return;
}

void websocket_write(server_pt srv,
                     int fd,
                     void* data,
                     size_t len,
                     char text,
                     char first,
                     char last) {
  if (len < 126) {
    struct {
      unsigned op_code : 4;
      unsigned rsv3 : 1;
      unsigned rsv2 : 1;
      unsigned rsv1 : 1;
      unsigned fin : 1;
      unsigned size : 7;
      unsigned masked : 1;
    } head = {.op_code = (first ? (!text ? 2 : text) : 0),
              .fin = last,
              .size = len,
              .masked = 0};
    void* buff = malloc(len + 2);
    if (!buff) {
      fprintf(stderr,
              "ERROR: Couldn't allocate memory for buffer (writing websocket "
              "data to network).\n");
      return;
    }
    memcpy(buff, &head, 2);
    memcpy(buff + 2, data, len);
    Server.write(srv, fd, buff, len + 2);
  } else if (len <= 65532) {
    /* head is 4 bytes */
    struct {
      unsigned op_code : 4;
      unsigned rsv3 : 1;
      unsigned rsv2 : 1;
      unsigned rsv1 : 1;
      unsigned fin : 1;
      unsigned size : 7;
      unsigned masked : 1;
      unsigned length : 16;
    } head = {.op_code = (first ? (text ? 1 : 2) : 0),
              .fin = last,
              .size = 126,
              .masked = 0,
              .length = htons(len)};
    /* head is 4 bytes */
    void* buff = malloc(len + 4);
    if (!buff) {
      fprintf(stderr,
              "ERROR: Couldn't allocate memory for buffer (writing websocket "
              "data to network).\n");
      return;
    }
    memcpy(buff, &head, 4);
    memcpy(buff + 4, data, len);
    Server.write(srv, fd, buff, len + 4);
  } else {
    /* fragmentation is better */
    while (len > 65532) {
      websocket_write(srv, fd, data, 65532, text, first, 0);
      data += 65532;
      first = 0;
    }
    websocket_write(srv, fd, data, len, text, first, 1);
  }
  return;
}

//////////////////////////////////////
// Protocol Instance Methods

// This method sends a close frame and closes the websocket connection.
static VALUE ws_close(VALUE self) {
  server_pt srv = get_server(self);
  int fd = FIX2INT(rb_ivar_get(self, fd_var_id));
  websocket_close(srv, fd);
  return Qnil;
}

/* This method writes the data to the websocket.

If the data isn't UTF8 encoded (the default encoding), it will be sent as
binary data according to the websocket protocol framing rules.

i.e.

      # sending a regular websocket text frame (UTF-8)
      write "This is a text message"
       # sending a binary websocket frame
      data = "binary"
      data.force_encoding("binary")
      write data
 */
static VALUE ws_write(VALUE self, VALUE data) {
  if (!data || data == Qnil)
    return self;
  server_pt srv = get_server(self);
  int fd = FIX2INT(rb_ivar_get(self, fd_var_id));
  websocket_write(srv, fd, RSTRING_PTR(data), RSTRING_LEN(data),
                  (rb_enc_get_index(data) == UTF8EncodingIndex), 1, 1);
  return self;
}

/*
Similar to {each}, the block passed to `each` will be called for every connected
websocket's
handler.

However, unlike {each}, the code will be executed within <b>this</b>
connection's lock, in a blocking manner, allowing other connections to
concurrently perform other tasks.

This option will require a significanltly lower amount of resources to perform,
but due to the concurrency variation should only be used if the other
connection's data isn't important (i.e., directly sending messages through the
other connection sockets).

i.e. , here is a simple blocking websocket broadcasting service:

      def MyBroadcast
        def on_message data
          # this will also broadcast to `self`
          each_block {|h| h.write data}
        end
      end

      srv = Iodine::Http.new
      srv.on_websocket = Proc.new {|env| MyBroadcast }

Notice that this is process ("worker") specific, this does not affect
connections that are connected to a different process on the same machine or a
different machine running the same application.
*/
static VALUE ws_each_block(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  // get the server object
  struct Server* srv = get_server(self);
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qnil;
  Server.each_block(srv, ws_service_name, each_protocol_block, (void*)block);
  return block;
}

/*
The block passed to `each` will be called for every connected websocket's
handler.

Each connection will perform the block within it's own connection's lock, so
that no single connection is performing more then a single task at a time.

i.e. , here is a simple websocket broadcasting service:

      def MyBroadcast
        def on_message data
          # this will also broadcast to `self`
          each {|h| h.write data}
        end
      end

      srv = Iodine::Http.new
      srv.on_websocket = Proc.new {|env| MyBroadcast }

Notice that this is process ("worker") specific, this does not affect
connections that are connected to a different process on the same machine or a
different machine running the same application.
*/
static VALUE ws_each(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  // get the server object
  struct Server* srv = get_server(self);
  // requires multi-threading
  if (Server.settings(srv)->threads < 0) {
    rb_warn(
        "called an async method in a non-async mode - the task will "
        "be performed immediately.");
    return ws_each_block(self);
  }
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qnil;
  Registry.add(block);
  Server.each_block(srv, ws_service_name, each_protocol_async_schedule,
                    (void*)block);
  Registry.remove(block);
  return block;
}

/**
Returns the number of total websocket connections in this specific process.

Notice that this is process ("worker") specific, this does not affect
connections that are connected to a different process on the same machine or a
different machine running the same application.
*/
static VALUE ws_count(VALUE self) {
  server_pt srv = get_server(self);
  if (!srv)
    rb_raise(rb_eRuntimeError, "Server isn't running.");
  return LONG2FIX(Server.count(srv, ws_service_name));
}

//////////////////////////////////////
// Protocol Callbacks
static void on_close(server_pt server, int sockfd) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  if (!ws)
    return;
  Server.set_udata(server, sockfd, NULL);
  RubyCaller.call(ws->handler, on_close_func_id);
  WebsocketProtocol_destroy(ws);
}
void on_shutdown(server_pt server, int sockfd) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  if (ws) {
    RubyCaller.call(ws->handler, on_shutdown_func_id);
    websocket_close(server, sockfd);
  }
}

void ping(server_pt server, int sockfd) {
  // struct WebsocketProtocol* ws =
  //     (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  Server.write_urgent(server, sockfd, "\x89\x00", 2);
}

void on_data(server_pt server, int sockfd) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  if (!ws)
    return;
  ssize_t len = 0;
  int pos = 0;
  int data_len = 0;
  size_t buf_pos = 0;
  size_t tmp = 0;
  while ((len = Server.read(sockfd, ws->parser.tmp_buffer, 1024)) > 0) {
    data_len = 0;
    pos = 0;
    while (pos < len) {
      // collect the frame's head
      if (!(*(char*)(&ws->parser.head))) {
        *((char*)(&(ws->parser.head))) = ws->parser.tmp_buffer[pos];
        // advance
        pos++;
        // go back to the `while` head, to review if there's more data
        continue;
      }
      // save a copy if it's the first head in a fragmented message
      if (!(*(char*)(&ws->parser.head2))) {
        ws->parser.head2 = ws->parser.head;
      }

      // save the mask and size information
      if (!(*(char*)(&ws->parser.sdata))) {
        *((char*)(&(ws->parser.sdata))) = ws->parser.tmp_buffer[pos];
        // set length
        if (!IS_BIG_ENDIAN)
          ws->parser.state.at_len = ws->parser.sdata.size == 127
                                        ? 7
                                        : ws->parser.sdata.size == 126 ? 1 : 0;
        else
          ws->parser.state.at_len = 0;
        pos++;
        continue;
      }

      // check that if we need to collect the length data
      if (ws->parser.sdata.size >= 126 && !(ws->parser.state.has_len)) {
      // avoiding a loop so we don't mixup the meaning of "continue" and
      // "break"
      collect_len:
        ////////// NOTICE: Network Byte Order might mess this code up - test
        /// this
        if (IS_BIG_ENDIAN) {
          if ((ws->parser.state.at_len == 1 && ws->parser.sdata.size == 126) ||
              (ws->parser.state.at_len == 7 && ws->parser.sdata.size == 127)) {
            ws->parser.psize.bytes[ws->parser.state.at_len] =
                ws->parser.tmp_buffer[pos++];
            ws->parser.state.has_len = 1;
            ws->parser.length = (ws->parser.sdata.size == 126)
                                    ? ws->parser.psize.len1
                                    : ws->parser.psize.len2;
          } else {
            ws->parser.psize.bytes[ws->parser.state.at_len++] =
                ws->parser.tmp_buffer[pos++];
            if (pos < len)
              goto collect_len;
          }
        } else {
          if (ws->parser.state.at_len == 0) {
            ws->parser.psize.bytes[ws->parser.state.at_len] =
                ws->parser.tmp_buffer[pos++];
            ws->parser.state.has_len = 1;
            ws->parser.length = (ws->parser.sdata.size == 126)
                                    ? ws->parser.psize.len1
                                    : ws->parser.psize.len2;
          } else {
            ws->parser.psize.bytes[ws->parser.state.at_len--] =
                ws->parser.tmp_buffer[pos++];
            if (pos < len)
              goto collect_len;
          }
        }
        continue;
      }
      if (!ws->parser.length && ws->parser.sdata.size < 126)
        ws->parser.length = ws->parser.sdata.size;

      // check that the data is masked and thet we we didn't colleced the mask
      if (ws->parser.sdata.masked && !(ws->parser.state.has_mask)) {
      // avoiding a loop so we don't mixup the meaning of "continue" and
      // "break"
      collect_mask:
        if (ws->parser.state.at_mask == 3) {
          ws->parser.mask[ws->parser.state.at_mask] =
              ws->parser.tmp_buffer[pos++];
          ws->parser.state.has_mask = 1;
          ws->parser.state.at_mask = 0;
        } else {
          ws->parser.mask[ws->parser.state.at_mask++] =
              ws->parser.tmp_buffer[pos++];
          if (pos < len)
            goto collect_mask;
          else
            continue;
        }
        // since it's possible that there's no more data (0 length frame),
        // we don't go `continue` (check while loop) and we process what we
        // have.
      }

      // Now that we know everything about the frame, let's collect the data

      // How much data in the buffer is part of the frame?
      data_len = len - pos;
      if (data_len + ws->parser.received > ws->parser.length)
        data_len = ws->parser.length - ws->parser.received;

      // a note about unmasking: since ws->parser.state.at_mask is only 2 bits,
      // it will wrap around (i.e. 3++ == 0), so no modulus is required.
      // unmask:
      if (ws->parser.sdata.masked) {
        for (int i = 0; i < data_len; i++) {
          ws->parser.tmp_buffer[i + pos] ^=
              ws->parser.mask[ws->parser.state.at_mask++];
        }
      } else {
        // enforce masking?
      }
      // Copy the data to the Ruby buffer - only if it's a user message
      if (ws->parser.head.op_code == 1 || ws->parser.head.op_code == 2 ||
          (!ws->parser.head.op_code &&
           (ws->parser.head2.op_code == 1 || ws->parser.head2.op_code == 2))) {
        // get current Ruby Position length
        buf_pos = RSTRING_LEN(ws->buffer);
        // review buffer's capacity - it can only grow.
        tmp = rb_str_capacity(ws->buffer);
        if (buf_pos + ws->parser.length - ws->parser.received > tmp) {
          // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!! //
          // resizing the buffer MUST be performed within the GVL lock!
          // due to memory manipulation.
          // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!! //
          RubyCaller.call_c(resize_buffer_in_gvl, ws);
        }
        // copy here
        memcpy(RSTRING_PTR(ws->buffer) + buf_pos, ws->parser.tmp_buffer + pos,
               data_len);
        rb_str_set_len(ws->buffer, buf_pos + data_len);
      }
      // set the frame's data received so far (copied or not)
      // we couldn't do it soonet, because we needed the value to compute the
      // Ruby buffer capacity (within the GVL resize function).
      ws->parser.received += data_len;

      // check that we have collected the whole of the frame.
      if (ws->parser.length > ws->parser.received) {
        pos += data_len;
        continue;
      }

      // we have the whole frame, time to process the data.
      // pings, pongs and other non-Ruby handled messages.
      if (ws->parser.head.op_code == 0 || ws->parser.head.op_code == 1 ||
          ws->parser.head.op_code == 2) {
        /* a user data frame */
        if (ws->parser.head.fin) {
          /* This was the last frame */
          if (ws->parser.head2.op_code == 1) {
            /* text data */
            rb_enc_associate(ws->buffer, UTF8Encoding);
          } else if (ws->parser.head2.op_code == 2) {
            /* binary data */
            rb_enc_associate(ws->buffer, BinaryEncoding);
          } else  // not a recognized frame, don't act
            goto reset_parser;
          // call the on_message callback
          // we dont use:
          // // RubyCaller.call2(ws->handler, on_msg_func_id, 1, &(ws->buffer));
          // since we want to dup the buffer (Ruby's lazy copy)
          RubyCaller.call_c(on_message_in_gvl, ws);
          goto reset_parser;
        }
      } else if (ws->parser.head.op_code == 8) {
        /* close */
        websocket_close(server, sockfd);
        if (ws->parser.head2.op_code == ws->parser.head.op_code)
          goto reset_parser;
      } else if (ws->parser.head.op_code == 9) {
        /* ping */
        // write Pong - including ping data...
        websocket_write(server, sockfd, ws->parser.tmp_buffer + pos, data_len,
                        10, 1, 1);
        if (ws->parser.head2.op_code == ws->parser.head.op_code)
          goto reset_parser;
      } else if (ws->parser.head.op_code == 10) {
        /* pong */
        // do nothing... almost
        if (ws->parser.head2.op_code == ws->parser.head.op_code)
          goto reset_parser;
      } else if (ws->parser.head.op_code > 2 && ws->parser.head.op_code < 8) {
        /* future control frames. ignore. */
        if (ws->parser.head2.op_code == ws->parser.head.op_code)
          goto reset_parser;
      } else {
        /* WTF? */
        if (ws->parser.head.fin)
          goto reset_parser;
      }
      // not done, but move the pos marker along
      pos += data_len;
      continue;

    reset_parser:
      // move the pos marker along - in case we have more then one frame in the
      // buffer
      pos += data_len;
      // clear the parser
      *((char*)(&(ws->parser.head))) = 0;
      *((char*)(&(ws->parser.head2))) = 0;
      *((char*)(&(ws->parser.sdata))) = 0;
      *((char*)(&(ws->parser.state))) = 0;
      // the above is the same as
      // memset(&(ws->parser.head), 0, 4);
      // set the union size to 0
      ws->parser.psize.len2 = 0;
      ws->parser.length = 0;
      ws->parser.received = 0;
      // The Ruby buffer was cleared within the GVL, the following should be
      // safe now that we know the String isn't shared (it's here in case we got
      // here without forwarding the data).
      rb_str_set_len(ws->buffer, 0);
      // rb_enc_associate(ws->buffer, BinaryEncoding);
    }
  }
}

//////////////////////////////////////
// Protocol constructor

static struct WebsocketProtocol* WebsocketProtocol_new(void) {
  struct WebsocketProtocol* ws = malloc(sizeof(struct WebsocketProtocol));
  *ws = (struct WebsocketProtocol){
      .protocol.service = ws_service_name,  // set the service name for `each`
      .protocol.on_data = on_data,          // set the callback
      .protocol.on_shutdown = on_shutdown,  // set the callback
      .protocol.on_close = on_close,        // set the callback
      .protocol.ping = ping,                // set the callback
  };
  return ws;
}

// This should be called within the GVL, as it performs Ruby API calls
static void websocket_new(struct HttpRequest* request, VALUE handler) {
  struct WebsocketProtocol* ws = NULL;
  // check that we actually have a websocket handler
  if (handler == Qnil || handler == Qfalse)
    goto reject;
  // create the Websocket Protocol
  ws = WebsocketProtocol_new();
  if (!ws)
    goto reject;
  // make sure we have a valid handler, with the Websocket Protocol mixin.
  if (TYPE(handler) == T_CLASS) {
    // include the Protocol module
    // // do we neet to check?
    // if (rb_mod_include_p(handler, rWebsocket) == Qfalse)
    rb_include_module(handler, rWebsocket);
    handler = RubyCaller.call(handler, new_func_id);
    // check that we created a handler
    if (handler == Qnil || handler == Qfalse) {
      goto reject;
    }
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    // // do we neet to check?
    // if (rb_mod_include_p(handler, rWebsocket) == Qfalse)
    rb_include_module(p_class, rWebsocket);
  }
  // set the Ruby handler for websocket messages
  ws->handler = handler;
  ws->buffer = rb_str_buf_new(2048);
  rb_str_set_len(ws->buffer, 0);
  rb_enc_associate(ws->buffer, BinaryEncoding);
  Registry.add(handler);
  Registry.add(ws->buffer);
  // setup server protocol and any data we need (i.e. timeout)
  Server.set_protocol(request->server, request->sockfd, (struct Protocol*)ws);
  Server.set_timeout(request->server, request->sockfd, Websockets.timeout);
  Server.touch(request->server, request->sockfd);
  // for the global each
  Server.set_udata(request->server, request->sockfd, (void*)ws->handler);
  // set the server and fd values for the handler... (used for `write` and
  // `close`)
  rb_ivar_set(handler, fd_var_id, INT2FIX(request->sockfd));
  set_server(handler, request->server);
  // call the on_open callback
  RubyCaller.call(handler, on_open_func_id);
  return;
reject:
  if (ws)
    WebsocketProtocol_destroy(ws);
  websocket_close(request->server, request->sockfd);
  return;
}

//////////////
// Empty callbacks for default implementations.

//  Please override this method and implement your own callback.
static VALUE empty_func(VALUE self) {
  return Qnil;
}
/* The `on_message(data)` callback is the main method for any websocket
implementation.

<b>NOTICE</b>: the data passed to the `on_message` callback is the actual
recycble network buffer, not a copy! <b>Use `data.dup` before moving the data
out of the function's scope</b> to prevent data corruption (i.e. when
using the data within an `each` block). For example (broadcasting):

        data = data.dup
        each {|ws| ws.write data }

Please override this method and implement your own callback.
*/
static VALUE def_dyn_message(VALUE self, VALUE data) {
  return Qnil;
}

/////////////////////////////
// initialize the class and the whole of the Iodine/http library
static void Init_websocket(void) {
  // get IDs and data that's used often
  call_proc_id = rb_intern("call");          // used to call the main callback
  server_var_id = rb_intern("server");       // when upgrading
  fd_var_id = rb_intern("sockfd");           // when upgrading
  dup_func_id = rb_intern("dup");            // when upgrading
  new_func_id = rb_intern("new");            // when upgrading
  on_open_func_id = rb_intern("on_open");    // when upgrading
  on_close_func_id = rb_intern("on_close");  // method ID
  on_shutdown_func_id = rb_intern("on_shutdown");  // a callback's ID
  on_msg_func_id = rb_intern("on_message");        // a callback's ID
  UTF8Encoding = rb_enc_find("UTF-8");             // sets encoding for data
  UTF8EncodingIndex = rb_enc_find_index("UTF-8");  // sets encoding for data

  // the Ruby websockets protocol class.
  rWebsocket = rb_define_module_under(rHttp, "WebsocketProtocol");
  if (rWebsocket == Qfalse)
    fprintf(stderr, "WTF?!\n");
  // // callbacks and handlers
  rb_define_method(rWebsocket, "on_open", empty_func, 0);
  rb_define_method(rWebsocket, "on_message", def_dyn_message, 1);
  rb_define_method(rWebsocket, "on_shutdown", empty_func, 0);
  rb_define_method(rWebsocket, "on_close", empty_func, 0);
  // // helper methods
  iodine_add_helper_methods(rWebsocket);
  rb_define_method(rWebsocket, "write", ws_write, 1);
  rb_define_method(rWebsocket, "close", ws_close, 0);
  rb_define_method(rWebsocket, "each", ws_each, 0);
  rb_define_method(rWebsocket, "each_block", ws_each_block, 0);

  rb_define_method(rWebsocket, "ws_count", ws_count, 0);
}

struct __Websockets__CLASS__ Websockets = {
    .timeout = 45,
    .max_msg_size = 65536,
    .init = Init_websocket,
    .new = websocket_new,
};
