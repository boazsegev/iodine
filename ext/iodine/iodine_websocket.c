#include "iodine.h"
#include "iodine_http.h"
#include "iodine_websocket.h"
#include "rb-call.h"
#include "rb-registry.h"
#include <ruby/io.h>
#include <arpa/inet.h>

//////////////
// general global definitions we will use herein.
static VALUE rWebsocket;           // The Iodine::Http::Websocket class
static rb_encoding* UTF8Encoding;  // encoding object
static int UTF8EncodingIndex;
static ID buff_var_id;          // id for websocket buffer
static ID ws_var_id;            // id for websocket pointer
static ID call_proc_id;         // id for `#call`
static ID dup_func_id;          // id for the buffer.dup method
static ID new_func_id;          // id for the Class.new method
static ID on_open_func_id;      // the on_open callback's ID
static ID on_close_func_id;     // the on_close callback's ID
static ID on_shutdown_func_id;  // a callback's ID
static ID on_msg_func_id;       // a callback's ID

/*******************************************************************************
Buffer management - update to change the way the buffer is handled.
*/
struct buffer_s {
  void* data;
  size_t size;
};

/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s create_ws_buffer(ws_s* owner);

/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s resize_ws_buffer(ws_s* owner, struct buffer_s);

/** releases an existing buffer. */
void free_ws_buffer(ws_s* owner, struct buffer_s);

/** Sets the initial buffer size. (16Kb)*/
#define WS_INITIAL_BUFFER_SIZE 16384

// buffer increments by 4,096 Bytes (4Kb)
#define round_up_buffer_size(size) (((size) >> 12) + 1) << 12

struct buffer_args {
  struct buffer_s buffer;
  ws_s* ws;
};

void* ruby_land_buffer(void* _buf) {
  struct buffer_args* args = _buf;
  if (args->buffer.data) {
    round_up_buffer_size(args->buffer.size);
    VALUE rbbuff =
        rb_ivar_get((VALUE)Websocket.get_udata(args->ws), buff_var_id);
    rb_str_modify(rbbuff);
    rb_str_resize(rbbuff, args->buffer.size);
    args->buffer.data = RSTRING_PTR(rbbuff);
    args->buffer.size = rb_str_capacity(rbbuff);
  } else {
    VALUE rbbuff = rb_str_buf_new(WS_INITIAL_BUFFER_SIZE);
    rb_ivar_set((VALUE)Websocket.get_udata(args->ws), buff_var_id, rbbuff);
    rb_str_set_len(rbbuff, 0);
    rb_enc_associate(rbbuff, BinaryEncoding);
    args->buffer.data = RSTRING_PTR(rbbuff);
    args->buffer.size = WS_INITIAL_BUFFER_SIZE;
  }
  return NULL;
}

struct buffer_s create_ws_buffer(ws_s* owner) {
  struct buffer_args args = {.ws = owner};
  RubyCaller.call_c(ruby_land_buffer, &args);
  return args.buffer;
}

struct buffer_s resize_ws_buffer(ws_s* owner, struct buffer_s buffer) {
  struct buffer_args args = {.ws = owner, .buffer = buffer};
  RubyCaller.call_c(ruby_land_buffer, &args);
  return args.buffer;
}
void free_ws_buffer(ws_s* owner, struct buffer_s buff) {}

#undef round_up_buffer_size

//////////////////////////////////////
// Ruby functions

// GC will call this to "free" the memory... which would be bad.
static void dont_free(void* obj) {}
// the data wrapper and the dont_free instruction callback
static struct rb_data_type_struct iodine_websocket_type = {
    .wrap_struct_name = "IodineWebsocketData",
    .function.dfree = (void (*)(void*))dont_free,
};
/** a macro helper function to embed a server pointer in an object */
#define set_ws(object, ws)         \
  rb_ivar_set((object), ws_var_id, \
              TypedData_Wrap_Struct(rServer, &iodine_websocket_type, (ws)))

/** a macro helper to get the server pointer embeded in an object */
#define get_ws(object) (ws_s*) DATA_PTR(rb_ivar_get((object), ws_var_id))

static VALUE ws_close(VALUE self) {
  // TODO get ws object
  ws_s* ws = get_ws(self);
  Websocket.close(ws);
  return self;
}

static VALUE ws_write(VALUE self, VALUE data) {
  // TODO get ws object
  ws_s* ws = get_ws(self);
  Websocket.write(ws, RSTRING_PTR(data), RSTRING_LEN(data),
                  rb_enc_get(data) == UTF8Encoding);
  return self;
}

static VALUE ws_count(VALUE self) {
  ws_s* ws = get_ws(self);
  Websocket.count(ws);
  return self;
}

static void rb_perform_ws_task(ws_s* ws, void* arg) {
  VALUE handler = (VALUE)Websocket.get_udata(ws);
  if (!handler)
    return;
  RubyCaller.call2((VALUE)arg, call_proc_id, 1, (VALUE*)&arg);
}

static void rb_finish_ws_task(ws_s* ws, void* arg) {
  Registry.remove((VALUE)arg);
}

static VALUE ws_each(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  ws_s* ws = get_ws(self);
  if (!ws)
    return Qnil;
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qnil;
  Registry.add(block);
  Websocket.each(ws, rb_perform_ws_task, (void*)block, rb_finish_ws_task);
  return block;
}

//////////////////////////////////////
// Protocol functions
void ws_on_open(ws_s* ws) {
  VALUE handler = (VALUE)Websocket.get_udata(ws);
  if (!handler)
    return;
  // TODO save ws to handler
  set_ws(handler, ws);
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
  VALUE buffer = rb_ivar_get(handler, buff_var_id);
  if (is_text)
    rb_enc_associate(buffer, UTF8Encoding);
  else
    rb_enc_associate(buffer, BinaryEncoding);
  rb_str_set_len(buffer, length);
  RubyCaller.call2(handler, on_msg_func_id, 1, &buffer);
}

//////////////////////////////////////
// Protocol constructor

void iodine_websocket_upgrade(struct HttpRequest* request,
                              struct HttpResponse* response,
                              VALUE handler) {
  fprintf(stderr, "Enter Upgrade.\n");
  // make sure we have a valid handler, with the Websocket Protocol mixin.
  if (handler == Qnil || handler == Qfalse) {
    response->status = 400;
    HttpResponse.send(response);
    fprintf(stderr, "No Handler!!!\n");
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
  fprintf(stderr, "calling upgrade!\n");
  websocket_upgrade(.request = request, .response = response,
                    .udata = (void*)handler, .on_close = ws_on_close,
                    .on_open = ws_on_open, .on_shutdown = ws_on_shutdown,
                    .on_message = ws_on_data);
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
void Init_iodine_websocket(void) {
  // get IDs and data that's used often
  call_proc_id = rb_intern("call");          // used to call the main callback
  ws_var_id = rb_intern("ws_ptr");           // when upgrading
  buff_var_id = rb_intern("ws_buffer");      // when upgrading
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
  rb_define_method(rWebsocket, "count", ws_count, 0);
}
