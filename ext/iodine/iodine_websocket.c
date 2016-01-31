#include "iodine_websocket.h"
#include <ruby/io.h>

/* ////////////////////////////////////////////////////////////
This file creates an HTTP server based on the Iodine libraries.

The server is (mostly) Rack compatible, except:

1. upgrade requests are handled using special upgrade handlers.
2. if a String is returned, it is a assumed to be a status 200 Html data?

//////////////////////////////////////////////////////////// */

//////////////
// general global definitions we will use herein.
static VALUE rWebsocket;        // The Iodine::Http::Websocket class
static ID server_var_id;        // id for the Server variable (pointer)
static ID fd_var_id;            // id for the file descriptor (Fixnum)
static ID call_proc_id;         // id for `#call`
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
      unsigned fin : 1;
      unsigned rsv1 : 1;
      unsigned rsv2 : 1;
      unsigned rsv3 : 1;
      unsigned op_code : 4;
    } head, head2;
    struct {
      unsigned masked : 1;
      unsigned size : 7;
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

//////////////////////////////////////
// Protocol Callbacks
void on_close(server_pt server, int sockfd) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  RubyCaller.call_unsafe(ws->handler, on_close_func_id);
  WebsocketProtocol_destroy(ws);
}
void on_shutdown(server_pt server, int sockfd) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  RubyCaller.call_unsafe(ws->handler, on_shutdown_func_id);
}

void ping(server_pt server, int sockfd) {
  // struct WebsocketProtocol* ws =
  //     (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  Server.write_urgent(server, sockfd, "\x89\x00", 2);
  // RubyCaller.call_unsafe(ws->handler, on_close_func_id);
}

void on_data(server_pt server, int sockfd) {
  struct WebsocketProtocol* ws =
      (struct WebsocketProtocol*)Server.get_protocol(server, sockfd);
  ssize_t len = 0;
  int pos = 0;
  while ((len = Server.read(sockfd, ws->parser.tmp_buffer, 1024)) > 0) {
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
        if ((ws->parser.state.at_len == 1 && ws->parser.sdata.size == 126) ||
            (ws->parser.state.at_len == 7 && ws->parser.sdata.size == 127)) {
          ws->parser.psize.bytes[ws->parser.state.at_len] =
              ws->parser.tmp_buffer[pos++];
          ws->parser.state.has_len = 1;
          ws->parser.length = (ws->parser.sdata.size == 126)
                                  ? (ws->parser.psize.len1 + 126)
                                  : (ws->parser.psize.len2 + 127);
        } else {
          ws->parser.psize.bytes[ws->parser.state.at_len++] =
              ws->parser.tmp_buffer[pos++];
          if (pos < len)
            goto collect_len;
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
        }
        continue;
      }
      // Now that we know everything about the frame, let's collect the data

      // a note about unmasking: since ws->parser.state.at_mask is only 2 bits,
      // it will wrap around (i.e. 3++ == 0), so no modulus is required.
      // unmask:
      if (ws->parser.sdata.masked) {
        for (size_t i = pos; i < len && i < ws->parser.length + pos; i++) {
          ws->parser.tmp_buffer[i] ^=
              ws->parser.mask[ws->parser.state.at_mask++];
        }
      } else {
        // enforce masking?
      }
      // pings, pongs and other non-Ruby handled messages.
      if (!ws->parser.head.op_code) {
        /* continuation frame */
        if (ws->parser.head.fin) {
          /* This was the last frame */
        }
      } else if (ws->parser.head.op_code == 1) {
        /* text data */
      } else if (ws->parser.head.op_code == 2) {
        /* binary data */
      } else if (ws->parser.head.op_code == 8) {
        /* close */
      } else if (ws->parser.head.op_code == 9) {
        /* ping */
      } else if (ws->parser.head.op_code == 10) {
        /* pong */
      } else if (ws->parser.head.op_code > 2 && ws->parser.head.op_code < 8) {
        /* future control frames. ignore. */
      } else {
        /* code */
      }

      // RubyCaller.call2(ws->handler, on_msg_func_id, 1, &(ws->buffer));

      continue;
    reset_parser:
      // clear the parser and call a handler, entering the GVL
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
    }
  }
}

//////////////////////////////////////
// Protocol constructor

static struct WebsocketProtocol* WebsocketProtocol_new(void) {
  struct WebsocketProtocol* ws = malloc(sizeof(struct WebsocketProtocol));
  *ws = (struct WebsocketProtocol){
      .protocol.service = ws_service_name,  // set the service name for `each`
      .protocol.on_close = on_close,        // set the destructor
  };
  return ws;
}

// This should be called within the GVL, as it performs Ruby API calls
void websocket_new(struct HttpRequest* request, VALUE handler) {
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
    handler = RubyCaller.call_unsafe(handler, new_func_id);
    // check that we created a handler
    if (handler == Qnil || handler == Qfalse)
      goto reject;
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
  Registry.add(handler);
  Registry.add(ws->buffer);
  // setup server protocol and any data we need (i.e. timeout)
  Server.set_protocol(request->server, request->sockfd, (struct Protocol*)ws);
  Server.set_timeout(request->server, request->sockfd, Websockets.timeout);
  Server.touch(request->server, request->sockfd);
  // set the server and fd values for the handler... (used for `write` and
  // `close`)
  rb_ivar_set(handler, fd_var_id, INT2FIX(request->sockfd));
  set_server(handler, request->server);
  // call the on_open callback
  RubyCaller.call_unsafe(handler, on_open_func_id);

reject:
  if (ws)
    WebsocketProtocol_destroy(ws);
  Websockets.reject(request);
  return;
}
void websocket_reject(struct HttpRequest* request) {
  static char bad_req[] =
      "HTTP/1.1 400 Bad HttpRequest\r\n"
      "Connection: closed\r\n"
      "Content-Length: 16\r\n\r\n"
      "Bad HTTP Request\r\n";
  fprintf(stderr, "WEBSOCKET REJECT\n");
  Server.write(request->server, request->sockfd, bad_req, sizeof(bad_req));
  Server.close(request->server, request->sockfd);
  return;
}

//////////////
// Empty callbacks for default implementations.

//  default callback - do nothing
static VALUE empty_func(VALUE self) {
  return Qnil;
}
//  default callback - do nothing
static VALUE def_dyn_message(VALUE self, VALUE data) {
  return Qnil;
}

/////////////////////////////
// initialize the class and the whole of the Iodine/http library
void Init_websocket(void) {
  // get IDs and data that's used often
  call_proc_id = rb_intern("call");          // used to call the main callback
  server_var_id = rb_intern("server");       // when upgrading
  fd_var_id = rb_intern("sockfd");           // when upgrading
  new_func_id = rb_intern("new");            // when upgrading
  on_open_func_id = rb_intern("on_open");    // when upgrading
  on_close_func_id = rb_intern("on_close");  // method ID
  on_shutdown_func_id = rb_intern("on_shutdown");  // a callback's ID
  on_msg_func_id = rb_intern("on_message");        // a callback's ID

  // the Ruby websockets protocol class.
  rWebsocket = rb_define_module_under(rHttp, "WebsocketProtocol");
  // // callbacks and handlers
  rb_define_method(rWebsocket, "on_open", empty_func, 0);
  rb_define_method(rWebsocket, "on_message", def_dyn_message, 1);
  rb_define_method(rWebsocket, "on_shutdown", empty_func, 0);
  rb_define_method(rWebsocket, "on_close", empty_func, 0);
  // // helper methods
  // add_helper_methods(rWebsocket);
  // rb_define_method(rWebsocket, "write", srv_write, 1);
  // rb_define_method(rWebsocket, "close", srv_close, 0);
  // rb_define_method(rWebsocket, "each", srv_close, 0);
}

struct __Websockets__CLASS__ Websockets = {
    .timeout = 45,
    .max_msg_size = 65536,
    .init = Init_websocket,
    .new = websocket_new,
    .reject = websocket_reject,
};
