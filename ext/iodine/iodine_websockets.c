/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_websockets.h"
#include "iodine_pubsub.h"

#include "pubsub.h"
#include "websockets.h"

#include <arpa/inet.h>
#include <ruby/io.h>

/* *****************************************************************************
Core helpers and data
***************************************************************************** */

static VALUE IodineWebsocket; // The Iodine::Http::Websocket class
static ID ws_var_id;          // id for websocket pointer
static ID dup_func_id;        // id for the buffer.dup method

#define set_uuid(object, uuid)                                                 \
  rb_ivar_set((object), iodine_fd_var_id, ULONG2NUM((uuid)))

inline static intptr_t get_uuid(VALUE obj) {
  VALUE i = rb_ivar_get(obj, iodine_fd_var_id);
  return i != Qnil ? (intptr_t)FIX2ULONG(i) : 0;
}

#define set_ws(object, ws)                                                     \
  rb_ivar_set((object), ws_var_id, ULONG2NUM(((VALUE)(ws))))

inline static ws_s *get_ws(VALUE obj) {
  VALUE i = rb_ivar_get(obj, ws_var_id);
  if (i == Qnil)
    return NULL;
  return (ws_s *)FIX2ULONG(i);
}

#define set_handler(ws, handler) websocket_udata_set((ws), (VALUE)handler)
#define get_handler(ws) ((VALUE)websocket_udata((ws_s *)(ws)))

/* *****************************************************************************
Websocket Ruby API
***************************************************************************** */

/** Closes the websocket connection. The connection is only closed after
 * existing data in the outgoing buffer is sent. */
static VALUE iodine_ws_close(VALUE self) {
  void *ws = get_ws(self);
  if (!ws) {
    return Qfalse;
  }
  iodine_pubsub_type_e c_type = (iodine_pubsub_type_e)iodine_get_cdata(self);
  switch (c_type) {
  case IODINE_PUBSUB_WEBSOCKET:
    /* WebSockets*/
    if (((protocol_s *)ws)->service != WEBSOCKET_ID_STR) {
      return Qfalse;
    }
    websocket_close(ws);
    break;
  case IODINE_PUBSUB_SSE:
    /* SSE */
    http_sse_close(ws);
    break;
  case IODINE_PUBSUB_GLOBAL:
  /* fallthrough */
  default:
    return Qfalse;
    break;
  }
  return self;
}

/**
 * Writes data to the websocket.
 *
 * Returns `true` on success or `false if the websocket was closed or an error
 * occurred.
 *
 * `write` will return immediately, adding the data to the outgoing queue.
 *
 * If the connection is closed, `write` will raise an exception.
 */
static VALUE iodine_ws_write(VALUE self, VALUE data) {
  Check_Type(data, T_STRING);
  void *ws = get_ws(self);
  iodine_pubsub_type_e c_type = (iodine_pubsub_type_e)iodine_get_cdata(self);
  if (!ws || !c_type) {
    rb_raise(rb_eIOError, "Connection is closed");
    return Qfalse;
  }
  switch (c_type) {
  case IODINE_PUBSUB_WEBSOCKET:
    /* WebSockets*/
    if (((protocol_s *)ws)->service != WEBSOCKET_ID_STR)
      goto error;
    websocket_write(ws, RSTRING_PTR(data), RSTRING_LEN(data),
                    rb_enc_get(data) == IodineUTF8Encoding);
    return Qtrue;
    break;
  case IODINE_PUBSUB_SSE:
    /* SSE */
    http_sse_write(
        ws, .data = {.data = RSTRING_PTR(data), .len = RSTRING_LEN(data)});
    return Qtrue;
    break;
  case IODINE_PUBSUB_GLOBAL:
  /* fallthrough */
  default:
  error:
    rb_raise(rb_eIOError, "Connection is closed");
    return Qfalse;
  }
  return Qfalse;
}

/**
Returns a weak indication as to the state of the socket's buffer. If the server
has data in the buffer that wasn't written to the socket, `has_pending?` will
return `true`, otherwise `false` will be returned.
*/
static VALUE iodine_ws_has_pending(VALUE self) {
  intptr_t uuid = get_uuid(self);
  return SIZET2NUM(sock_pending(uuid));
}

/**
Returns a connection's UUID which is valid for *this process* (not a machine
or internet unique value).

This can be used together with a true process wide UUID to uniquely identify a
connection across the internet.
*/
static VALUE iodine_ws_uuid(VALUE self) {
  intptr_t uuid = get_uuid(self);
  return LONG2FIX(uuid);
}

/**
Returns true is the connection is open, false if it isn't.
*/
static VALUE iodine_ws_is_open(VALUE self) {
  intptr_t uuid = get_uuid(self);
  if (uuid && sock_isvalid(uuid))
    return Qtrue;
  return Qfalse;
}

/* *****************************************************************************
Websocket defer
***************************************************************************** */

static void iodine_perform_defer(intptr_t uuid, protocol_s *protocol,
                                 void *arg) {
  (void)(uuid);
  VALUE obj = protocol->service == WEBSOCKET_ID_STR ? get_handler(protocol)
                                                    : (VALUE)(protocol + 1);
  RubyCaller.call2((VALUE)arg, iodine_call_proc_id, 1, &obj);
  Registry.remove((VALUE)arg);
}

static void iodine_defer_fallback(intptr_t uuid, void *arg) {
  (void)(uuid);
  Registry.remove((VALUE)arg);
}

/**
Schedules a block of code to execute at a later time, **if** the connection is
still open and while preventing concurent code from running for the same
connection object.

An optional `conn_id` argument can be passed along, so that the block of code
will run for the requested connection rather then this connection.

**Careful**: as this might cause this connection's object to run code
concurrently when data owned by this connection is accessed from within the
block of code.

On success returns the block, otherwise (connection invalid) returns `false`. A
sucessful event registration doesn't guaranty that the block will be called (the
connection might close between the event registration and the execution).
*/
static VALUE iodine_defer(int argc, VALUE *argv, VALUE self) {
  intptr_t fd;
  // check arguments.
  if (argc > 1)
    rb_raise(rb_eArgError, "this function expects no more then 1 (optional) "
                           "argument.");
  else if (argc == 1) {
    Check_Type(*argv, T_FIXNUM);
    fd = FIX2LONG(*argv);
    if (!sock_isvalid(fd))
      return Qfalse;
  } else
    fd = iodine_get_fd(self);
  if (!fd)
    rb_raise(rb_eArgError, "This method requires a target connection.");
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);

  facil_defer(.uuid = fd, .task = iodine_perform_defer, .arg = (void *)block,
              .fallback = iodine_defer_fallback);
  return block;
}

/**
Schedules a block of code to run for the specified websocket at a later time,
(**if** the connection is open). The block will run within the connection's
lock, offering a fast concurrency synchronizing tool.

The block of code will receive the websocket's callback object. i.e.

    Iodine::Websocket.defer(uuid) {|ws| ws.write "I'm doing this" }

On success returns the block, otherwise (connection invalid) returns `false`.

A sucessful event registration doesn't guaranty that the block will be called
(the connection might close between the event registration and the execution).
*/
static VALUE iodine_class_defer(VALUE self, VALUE ws_uuid) {
  (void)(self);
  intptr_t fd = FIX2LONG(ws_uuid);
  if (!sock_isvalid(fd))
    return Qfalse;
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);

  facil_defer(.uuid = fd, .task = iodine_perform_defer, .arg = (void *)block,
              .fallback = iodine_defer_fallback);
  return block;
}

/* *****************************************************************************
Websocket Pub/Sub API
***************************************************************************** */

// clang-format off
/**
Subscribes the connection to a Pub/Sub channel.

The method accepts 1-2 arguments and an optional block. These are all valid ways
to call the method:

      subscribe("my_stream") {|from, msg| p msg }
      subscribe("my_stream", match: :redis) {|from, msg| p msg }
      subscribe(to: "my_stream")  {|from, msg| p msg }
      subscribe to: "my_stream", match: :redis, handler: MyProc

The first argument must be either a String or a Hash.

The second, optional, argument must be a Hash (if given).

The options Hash supports the following possible keys (other keys are ignored,
all keys are Symbols):

:match :: The channel / subject name matching type to be used. Valid value is: `:redis`. Future versions hope to support `:nats` and `:rabbit` patern matching as well.

:to :: The channel / subject to subscribe to.

:as :: valid for WebSocket connections only. can be either `:text` or `:binary`. `:text` is the default transport for pub/sub events.

Returns an {Iodine::PubSub::Subscription} object that answers to:

#.close :: closes the connection.

#.to_s :: returns the subscription's target (stream / channel / subject).

#.==(str) :: returns true if the string is an exact match for the target (even if the target itself is a pattern).

*/
static VALUE iodine_ws_subscribe(int argc, VALUE *argv, VALUE self) {
  // clang-format on
  ws_s *owner = get_ws(self);
  return iodine_subscribe(argc, argv, owner,
                          (iodine_pubsub_type_e)iodine_get_cdata(self));
}

/* *****************************************************************************
WebSocket Callbacks
***************************************************************************** */

static void ws_on_open(ws_s *ws) {
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  set_uuid(handler, websocket_uuid(ws));
  set_ws(handler, ws);
  iodine_set_cdata(handler, (void *)IODINE_PUBSUB_WEBSOCKET);
  RubyCaller.call(handler, iodine_on_open_func_id);
}
static void ws_on_close(intptr_t uuid, void *handler_) {
  VALUE handler = (VALUE)handler_;
  if (!handler) {
    fprintf(stderr,
            "ERROR: (iodine websockets) Closing a handlerless websocket?!\n");
    return;
  }
  set_ws(handler, NULL);
  set_uuid(handler, 0);
  iodine_set_cdata(handler, (void *)IODINE_PUBSUB_GLOBAL);
  RubyCaller.call(handler, iodine_on_close_func_id);
  Registry.remove(handler);
  (void)uuid;
}
static void ws_on_shutdown(ws_s *ws) {
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, iodine_on_shutdown_func_id);
}
static void ws_on_ready(ws_s *ws) {
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, iodine_on_ready_func_id);
}

struct ws_on_data_args_s {
  ws_s *ws;
  void *data;
  size_t length;
  uint8_t is_text;
};
static void *ws_on_data_inGIL(void *args_) {
  struct ws_on_data_args_s *a = args_;
  VALUE handler = get_handler(a->ws);
  if (!handler) {
    fprintf(stderr, "ERROR: iodine can't find Websocket handler!\n");
    return NULL;
  }
  VALUE buffer = rb_str_new(a->data, a->length);
  if (a->is_text)
    rb_enc_associate(buffer, IodineUTF8Encoding);
  else
    rb_enc_associate(buffer, IodineBinaryEncoding);
  rb_funcallv(handler, iodine_on_message_func_id, 1, &buffer);
  return NULL;
}
static void ws_on_data(ws_s *ws, char *data, size_t length, uint8_t is_text) {
  struct ws_on_data_args_s a = {
      .ws = ws, .data = data, .length = length, .is_text = is_text};
  RubyCaller.call_c(ws_on_data_inGIL, &a);
  (void)(data);
}

//////////////
// Empty callbacks for default implementations.

/**  Please implement your own callback for this event. */
static VALUE empty_func(VALUE self) {
  (void)(self);
  return Qnil;
}

/* *****************************************************************************
SSE Callbacks
***************************************************************************** */

/**
 * The (optional) on_open callback will be called once the EventSource
 * connection is established.
 */
static void iodine_sse_on_open(http_sse_s *sse) {
  VALUE handler = (VALUE)sse->udata;
  if (!handler)
    return;
  set_uuid(handler, http_sse2uuid(sse));
  set_ws(handler, sse);
  iodine_set_cdata(handler, (void *)IODINE_PUBSUB_SSE);
  RubyCaller.call(handler, iodine_on_open_func_id);
}

/**
 * The (optional) on_ready callback will be after a the underlying socket's
 * buffer changes it's state to empty.
 *
 * If the socket's buffer is never used, the callback is never called.
 */
static void iodine_sse_on_ready(http_sse_s *sse) {
  VALUE handler = (VALUE)sse->udata;
  if (!handler)
    return;
  RubyCaller.call(handler, iodine_on_ready_func_id);
}

/**
 * The (optional) on_shutdown callback will be called if a connection is still
 * open while the server is shutting down (called before `on_close`).
 */
static void iodine_sse_on_shutdown(http_sse_s *sse) {
  VALUE handler = (VALUE)sse->udata;
  if (!handler)
    return;
  RubyCaller.call(handler, iodine_on_shutdown_func_id);
}
/**
 * The (optional) on_close callback will be called once a connection is
 * terminated or failed to be established.
 *
 * The `uuid` is the connection's unique ID that can identify the Websocket. A
 * value of `uuid == 0` indicates the Websocket connection wasn't established
 * (an error occured).
 *
 * The `udata` is the user data as set during the upgrade or using the
 * `websocket_udata_set` function.
 */
static void iodine_sse_on_close(http_sse_s *sse) {
  VALUE handler = (VALUE)sse->udata;
  if (!handler) {
    fprintf(stderr,
            "ERROR: (iodine websockets) Closing a handlerless websocket?!\n");
    return;
  }
  set_ws(handler, NULL);
  set_uuid(handler, 0);
  iodine_set_cdata(handler, IODINE_PUBSUB_GLOBAL);
  RubyCaller.call(handler, iodine_on_close_func_id);
  Registry.remove(handler);
}

/* *****************************************************************************
Upgrading
***************************************************************************** */

static VALUE iodine_prep_ws_handler(VALUE handler) {
  // make sure we have a valid handler, with the Websocket Protocol mixin.
  if (handler == Qnil || handler == Qfalse || TYPE(handler) == T_FIXNUM ||
      TYPE(handler) == T_STRING || TYPE(handler) == T_SYMBOL)
    return Qnil;
  if (TYPE(handler) == T_CLASS || TYPE(handler) == T_MODULE) {
    // include the Protocol module
    rb_include_module(handler, IodineWebsocket);
    rb_extend_object(handler, IodineWebsocket);
    handler = RubyCaller.call(handler, iodine_new_func_id);
    if (handler == Qnil || handler == Qfalse)
      return Qnil;
    // check that we created a handler
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    rb_include_module(p_class, IodineWebsocket);
    rb_extend_object(p_class, IodineWebsocket);
  }
  return handler;
}

void iodine_upgrade_websocket(http_s *h, VALUE handler) {
  // add the handler to the registry
  handler = iodine_prep_ws_handler(handler);
  if (handler == Qnil)
    goto failed;
  Registry.add(handler);
  // send upgrade response and set new protocol
  http_upgrade2ws(.http = h, .udata = (void *)handler, .on_close = ws_on_close,
                  .on_open = ws_on_open, .on_shutdown = ws_on_shutdown,
                  .on_ready = ws_on_ready, .on_message = ws_on_data);
  return;
failed:
  http_send_error(h, 400);
  return;
}

void iodine_upgrade_sse(http_s *h, VALUE handler) {
  // add the handler to the registry
  handler = iodine_prep_ws_handler(handler);
  if (handler == Qnil)
    goto failed;
  Registry.add(handler);
  // send upgrade response and set new protocol
  http_upgrade2sse(h, .udata = (void *)handler, .on_open = iodine_sse_on_open,
                   .on_ready = iodine_sse_on_ready,
                   .on_shutdown = iodine_sse_on_shutdown,
                   .on_close = iodine_sse_on_close);
  return;
failed:
  http_send_error(h, 400);
  return;
}

/* *****************************************************************************
Initialization
***************************************************************************** */

void Iodine_init_websocket(void) {
  // get IDs and data that's used often
  ws_var_id = rb_intern("iodine_ws_ptr"); // when upgrading
  dup_func_id = rb_intern("dup");         // when upgrading

  // the Ruby websockets protocol class.
  IodineWebsocket = rb_define_module_under(Iodine, "Websocket");
  if (IodineWebsocket == Qfalse)
    fprintf(stderr, "WTF?!\n"), exit(-1);
  // // callbacks and handlers
  rb_define_method(IodineWebsocket, "on_open", empty_func, 0);

  // rb_define_method(IodineWebsocket, "on_message", empty_func_message, 1);

  rb_define_method(IodineWebsocket, "on_shutdown", empty_func, 0);
  rb_define_method(IodineWebsocket, "on_close", empty_func, 0);
  rb_define_method(IodineWebsocket, "on_ready", empty_func, 0);
  rb_define_method(IodineWebsocket, "write", iodine_ws_write, 1);
  rb_define_method(IodineWebsocket, "close", iodine_ws_close, 0);

  // rb_define_method(IodineWebsocket, "_c_id", iodine_ws_uuid, 0);

  rb_define_method(IodineWebsocket, "pending", iodine_ws_has_pending, 0);
  rb_define_method(IodineWebsocket, "open?", iodine_ws_is_open, 0);
  rb_define_method(IodineWebsocket, "defer", iodine_defer, -1);

  // rb_define_method(IodineWebsocket, "each", iodine_ws_each, 0);

  rb_define_method(IodineWebsocket, "subscribe", iodine_ws_subscribe, -1);
  rb_define_method(IodineWebsocket, "publish", iodine_publish, -1);

  // rb_define_singleton_method(IodineWebsocket, "defer", iodine_class_defer,
  // 1);

  rb_define_singleton_method(IodineWebsocket, "publish", iodine_publish, -1);
}
