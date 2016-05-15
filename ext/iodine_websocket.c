#include "iodine.h"
#include "iodine_websocket.h"
#include "rb-call.h"
#include "rb-registry.h"
#include <ruby/io.h>
#include <arpa/inet.h>

/* ////////////////////////////////////////////////////////////
This file creates an HTTP server based on the Iodine libraries.

The server is (mostly) Rack compatible, except:

1. upgrade requests are handled using special upgrade handlers.
2. if a String is returned, it is a assumed to be a status 200 Html data?

//////////////////////////////////////////////////////////// */

/*******************************************************************************
Buffer management - update to change the way the buffer is handled.
*/
struct buffer_s {
  void* data;
  size_t size;
  VALUE extra;
};

/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s create_ws_buffer(size_t size);
/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s resize_ws_buffer(struct buffer_s);
/** releases an existing buffer. */
void free_ws_buffer(struct buffer_s);
/** Sets the initial buffer size. (16Kb)*/
#define WS_INITIAL_BUFFER_SIZE 16384

/** buffer increments by 4,096 Bytes (4Kb) */
#define round_up_buffer_size(size) (((size) >> 12) + 1) << 12

struct buffer_s create_ws_buffer(size_t size) {
  struct buffer_s buff;
  buff.size = round_up_buffer_size(size);
  buff.data = malloc(buff.size);
  // buff.extra = rb_str_buf_new(2048);
  // rb_str_set_len(buff.extra, 0);
  // rb_enc_associate(buff.extra, BinaryEncoding);
  // Registry.add(buff.extra);
  return buff;
}

struct buffer_s resize_ws_buffer(struct buffer_s buff) {
  buff.size = round_up_buffer_size(buff.size);
  void* tmp = realloc(buff.data, buff.size);
  if (!tmp) {
    free_ws_buffer(buff);
    buff.size = 0;
  }
  buff.data = tmp;
  return buff;
}
void free_ws_buffer(struct buffer_s buff) {
  if (buff.data)
    free(buff.data);
  // Registry.remove(buff.extra);
}

#undef round_up_buffer_size

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

//////////////////////////////////////
// Protocol functions
void ws_on_open(ws_s* ws) {
  VALUE handler = (VALUE)Websocket.get_udata(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, on_open_func_id);
}
void ws_on_close(ws_s* ws) {
  VALUE handler = (VALUE)Websocket.get_udata(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, on_close_func_id);
  Registry.remove(handler);
}
void ws_on_shutdown(ws_s* ws) {
  VALUE handler = (VALUE)Websocket.get_udata(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, on_shutdown_func_id);
}
void ws_on_data(ws_s* ws, char* data, size_t length, int is_text) {
  VALUE handler = (VALUE)Websocket.get_udata(ws);
  if (!handler)
    return;
  RubyCaller.call2(handler, on_msg_func_id, 0, NULL);
}

//////////////////////////////////////
// Protocol constructor

void iodine_websocket_upgrade(struct HttpRequest* request,
                              struct HttpResponse* response,
                              VALUE handler) {
  // make sure we have a valid handler, with the Websocket Protocol mixin.
  if (handler == Qnil || handler == Qfalse) {
    response->status = 400;
    HttpResponse.send(response);
    return;
  }
  if (TYPE(handler) == T_CLASS) {
    // include the Protocol module
    rb_include_module(handler, rWebsocket);
    handler = RubyCaller.call(handler, new_func_id);
    // check that we created a handler
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    rb_include_module(p_class, rWebsocket);
  }
  // add the handler to the registry
  Registry.add(handler);
  // set the connection's udata
  Server.set_udata(request->server, request->sockfd, (void*)handler);
  // send upgrade response and set new protocol
  websocket_upgrade(.request = request, .response = response,
                    .udata = (void*)handler, .on_close = ws_on_close,
                    .on_open = ws_on_open, .on_shutdown = ws_on_shutdown,
                    .on_message = ws_on_data);
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
  if (Server.set_protocol(request->server, request->sockfd,
                          (struct Protocol*)ws))
    goto reject;  // reject on error
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
