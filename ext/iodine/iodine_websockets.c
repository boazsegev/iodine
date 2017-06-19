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

static VALUE force_var_id;
static VALUE channel_var_id;
static VALUE pattern_var_id;
static VALUE text_var_id;
static VALUE binary_var_id;
static VALUE engine_var_id;
static VALUE message_var_id;

#define set_uuid(object, request)                                              \
  rb_ivar_set((object), iodine_fd_var_id, ULONG2NUM((request)->fd))

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
Buffer management - Rubyfy the way the buffer is handled.
***************************************************************************** */

struct buffer_s {
  void *data;
  size_t size;
};

/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s create_ws_buffer(ws_s *owner);

/** returns a buffer_s struct, with a buffer (at least) `size` long. */
struct buffer_s resize_ws_buffer(ws_s *owner, struct buffer_s);

/** releases an existing buffer. */
void free_ws_buffer(ws_s *owner, struct buffer_s);

/** Sets the initial buffer size. (4Kb)*/
#define WS_INITIAL_BUFFER_SIZE 4096

// buffer increments by 4,096 Bytes (4Kb)
#define round_up_buffer_size(size) ((((size) >> 12) + 1) << 12)

struct buffer_args {
  struct buffer_s buffer;
  ws_s *ws;
};

void *ruby_land_buffer(void *_buf) {
#define args ((struct buffer_args *)(_buf))
  if (args->buffer.data == NULL) {
    VALUE rbbuff = rb_str_buf_new(WS_INITIAL_BUFFER_SIZE);
    rb_ivar_set(get_handler(args->ws), iodine_buff_var_id, rbbuff);
    rb_str_set_len(rbbuff, 0);
    rb_enc_associate(rbbuff, IodineBinaryEncoding);
    args->buffer.data = RSTRING_PTR(rbbuff);
    args->buffer.size = WS_INITIAL_BUFFER_SIZE;

  } else {
    VALUE rbbuff = rb_ivar_get(get_handler(args->ws), iodine_buff_var_id);
    rb_str_modify(rbbuff);
    rb_str_resize(rbbuff, args->buffer.size);
    args->buffer.data = RSTRING_PTR(rbbuff);
    args->buffer.size = rb_str_capacity(rbbuff);
  }
  return NULL;
#undef args
}

struct buffer_s create_ws_buffer(ws_s *owner) {
  struct buffer_args args = {.ws = owner};
  RubyCaller.call_c(ruby_land_buffer, &args);
  return args.buffer;
}

struct buffer_s resize_ws_buffer(ws_s *owner, struct buffer_s buffer) {
  buffer.size = round_up_buffer_size(buffer.size);
  struct buffer_args args = {.ws = owner, .buffer = buffer};
  RubyCaller.call_c(ruby_land_buffer, &args);
  return args.buffer;
}
void free_ws_buffer(ws_s *owner, struct buffer_s buff) {
  (void)(owner);
  (void)(buff);
}

#undef round_up_buffer_size

/* *****************************************************************************
Websocket Ruby API
***************************************************************************** */

/** Closes the websocket connection. The connection is only closed after
 * existing data in the outgoing buffer is sent. */
static VALUE iodine_ws_close(VALUE self) {
  ws_s *ws = get_ws(self);
  if (!ws || ((protocol_s *)ws)->service != WEBSOCKET_ID_STR)
    return Qfalse;
  websocket_close(ws);
  return self;
}

/**
 * Writes data to the websocket.
 *
 * Returns `true` on success or `false if the websocket was closed or an error
 * occurred.
 *
 * `write` will return immediately UNLESS resources are insufficient. If the
 * global `write` buffer is full, `write` will block until a buffer "packet"
 * becomes available and can be assigned to the socket. */
static VALUE iodine_ws_write(VALUE self, VALUE data) {
  Check_Type(data, T_STRING);
  ws_s *ws = get_ws(self);
  // if ((void *)ws == (void *)0x04 || (void *)data == (void *)0x04 ||
  //     RSTRING_PTR(data) == (void *)0x04)
  //   fprintf(stderr, "iodine_ws_write: self = %p ; data = %p\n"
  //                   "\t\tString ptr: %p, String length: %lu\n",
  //           (void *)ws, (void *)data, RSTRING_PTR(data), RSTRING_LEN(data));
  if (!ws || ((protocol_s *)ws)->service != WEBSOCKET_ID_STR)
    return Qfalse;
  websocket_write(ws, RSTRING_PTR(data), RSTRING_LEN(data),
                  rb_enc_get(data) == IodineUTF8Encoding);
  return Qtrue;
}

/** Returns the number of active websocket connections (including connections
 * that are in the process of closing down). */
static VALUE iodine_ws_count(VALUE self) {
  return LONG2FIX(websocket_count());
  (void)self;
}

/**
Returns a weak indication as to the state of the socket's buffer. If the server
has data in the buffer that wasn't written to the socket, `has_pending?` will
return `true`, otherwise `false` will be returned.
*/
static VALUE iodine_ws_has_pending(VALUE self) {
  intptr_t uuid = get_uuid(self);
  return sock_has_pending(uuid) ? Qtrue : Qfalse;
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

/* *****************************************************************************
Websocket Pub/Sub API
***************************************************************************** */

static void *on_pubsub_notificationinGVL(websocket_pubsub_notification_s *n) {
  VALUE rbn[2];
  rbn[0] = rb_str_new(n->channel.name, n->channel.len);
  Registry.add(rbn[0]);
  rbn[1] = rb_str_new(n->msg.data, n->msg.len);
  Registry.add(rbn[1]);
  RubyCaller.call2((VALUE)n->udata, iodine_call_proc_id, 2, rbn);
  Registry.remove(rbn[0]);
  Registry.remove(rbn[1]);
  return NULL;
}

static void on_pubsub_notificationin(websocket_pubsub_notification_s n) {
  RubyCaller.call_c((void *(*)(void *))on_pubsub_notificationinGVL, &n);
}

/**
Subscribes the websocket to a channel belonging to a specific pub/sub service
(using an {Iodine::PubSub::Engine} to connect Iodine to the service).

The function accepts a single argument (a Hash) and an optional block.

If no block is provided, the message is sent directly to the websocket client.

Accepts a single Hash argument with the following possible options:

:engine :: If provided, the engine to use for pub/sub. Otherwise the default
engine is used.

:channel :: Required (unless :pattern). The channel to subscribe to.

:pattern :: An alternative to the required :channel, subscribes to a pattern.

:force :: This can be set to either nil, :text or :binary and controls the way
the message will be forwarded to the websocket client. This is only valid if no
block was provided. Defaults to smart encoding based testing.


*/
static VALUE iodine_ws_subscribe(VALUE self, VALUE args) {
  Check_Type(args, T_HASH);
  ws_s *ws = get_ws(self);
  if (!ws || ((protocol_s *)ws)->service != WEBSOCKET_ID_STR)
    return Qfalse;
  uint8_t use_pattern = 0, force_text = 0, force_binary = 0;

  VALUE rb_ch = rb_hash_aref(args, channel_var_id);
  if (rb_ch == Qnil || rb_ch == Qfalse) {
    use_pattern = 1;
    rb_ch = rb_hash_aref(args, pattern_var_id);
    if (rb_ch == Qnil || rb_ch == Qfalse)
      rb_raise(rb_eArgError, "channel is required for pub/sub methods.");
  }
  Check_Type(rb_ch, T_STRING);

  VALUE tmp = rb_hash_aref(args, force_var_id);
  if (tmp == text_var_id)
    force_text = 1;
  else if (tmp == binary_var_id)
    force_binary = 1;
  Check_Type(rb_ch, T_STRING);

  VALUE block = 0;
  if (rb_block_given_p()) {
    block = rb_block_proc();
  }

  pubsub_engine_s *engine =
      iodine_engine_ruby2facil(rb_hash_aref(args, engine_var_id));

  uintptr_t subid = websocket_subscribe(
      ws, .channel.name = RSTRING_PTR(rb_ch), .channel.len = RSTRING_LEN(rb_ch),
      .engine = engine, .use_pattern = use_pattern, .force_text = force_text,
      .force_binary = force_binary,
      .on_message = (block ? on_pubsub_notificationin : NULL),
      .udata = (void *)block);
  if (!subid)
    return Qnil;
  return ULL2NUM(subid);
}
/**
Searches for the subscription ID for the describes subscription.

Takes the same arguments as {subscribe}, a single Hash argument with the
following possible options:

:engine :: If provided, the engine to use for pub/sub. Otherwise the default
engine is used.

:channel :: The subscription's channel.

:pattern :: An alternative to the required :channel, subscribes to a pattern.

:force :: This can be set to either nil, :text or :binary and controls the way
the message will be forwarded to the websocket client. This is only valid if no
block was provided. Defaults to smart encoding based testing.

*/
static VALUE iodine_ws_is_subscribed(VALUE self, VALUE args) {
  Check_Type(args, T_HASH);
  ws_s *ws = get_ws(self);
  if (!ws || ((protocol_s *)ws)->service != WEBSOCKET_ID_STR)
    return Qfalse;
  uint8_t use_pattern = 0, force_text = 0, force_binary = 0;

  VALUE rb_ch = rb_hash_aref(args, channel_var_id);
  if (rb_ch == Qnil || rb_ch == Qfalse) {
    use_pattern = 1;
    rb_ch = rb_hash_aref(args, pattern_var_id);
    if (rb_ch == Qnil || rb_ch == Qfalse)
      rb_raise(rb_eArgError, "channel is required for pub/sub methods.");
  }
  Check_Type(rb_ch, T_STRING);

  VALUE tmp = rb_hash_aref(args, force_var_id);
  if (tmp == text_var_id)
    force_text = 1;
  else if (tmp == binary_var_id)
    force_binary = 1;
  Check_Type(rb_ch, T_STRING);

  VALUE block = 0;
  if (rb_block_given_p()) {
    block = rb_block_proc();
  }

  pubsub_engine_s *engine =
      iodine_engine_ruby2facil(rb_hash_aref(args, engine_var_id));

  uintptr_t subid = websocket_find_sub(
      ws, .channel.name = RSTRING_PTR(rb_ch), .channel.len = RSTRING_LEN(rb_ch),
      .engine = engine, .use_pattern = use_pattern, .force_text = force_text,
      .force_binary = force_binary,
      .on_message = (block ? on_pubsub_notificationin : NULL),
      .udata = (void *)block);
  if (!subid)
    return Qnil;
  return LONG2NUM(subid);
}

/**
Cancels the subscription matching `sub_id`.
*/
static VALUE iodine_ws_unsubscribe(VALUE self, VALUE sub_id) {
  ws_s *ws = get_ws(self);
  if (!ws || ((protocol_s *)ws)->service != WEBSOCKET_ID_STR)
    return Qfalse;
  Check_Type(sub_id, T_FIXNUM);
  websocket_unsubscribe(ws, NUM2LONG(sub_id));
  return Qnil;
}

/**
Publishes a message to a channel.

Accepts a single Hash argument with the following possible options:

:engine :: If provided, the engine to use for pub/sub. Otherwise the default
engine is used.

:channel :: Required (unless :pattern). The channel to publish to.

:pattern :: An alternative to the required :channel, publishes to a pattern.
This is NOT supported by Redis and it's limited to the local process cluster.

:message :: REQUIRED. The message to be published.
:
*/
static VALUE iodine_ws_publish(VALUE self, VALUE args) {
  Check_Type(args, T_HASH);
  uint8_t use_pattern = 0;

  VALUE rb_ch = rb_hash_aref(args, channel_var_id);
  if (rb_ch == Qnil || rb_ch == Qfalse) {
    use_pattern = 1;
    rb_ch = rb_hash_aref(args, pattern_var_id);
    if (rb_ch == Qnil || rb_ch == Qfalse)
      rb_raise(rb_eArgError, "channel is required for pub/sub methods.");
  }
  Check_Type(rb_ch, T_STRING);

  VALUE rb_msg = rb_hash_aref(args, message_var_id);
  if (rb_msg == Qnil || rb_msg == Qfalse) {
    rb_raise(rb_eArgError, "message is required for the :publish method.");
  }
  Check_Type(rb_msg, T_STRING);

  pubsub_engine_s *engine =
      iodine_engine_ruby2facil(rb_hash_aref(args, engine_var_id));

  intptr_t subid =
      pubsub_publish(.engine = engine, .channel.name = (RSTRING_PTR(rb_ch)),
                     .channel.len = (RSTRING_LEN(rb_ch)),
                     .msg.data = (RSTRING_PTR(rb_msg)),
                     .msg.len = (RSTRING_LEN(rb_msg)),
                     .use_pattern = use_pattern);
  if (!subid)
    return Qfalse;
  return Qtrue;
  (void)self;
}

/* *****************************************************************************
Websocket Multi-Write - Deprecated
***************************************************************************** */

// static uint8_t iodine_ws_if_callback(ws_s *ws, void *block) {
//   if (!ws)
//     return 0;
//   VALUE handler = get_handler(ws);
//   uint8_t ret = 0;
//   if (handler)
//     ret = RubyCaller.call2((VALUE)block, iodine_call_proc_id, 1, &handler);
//   return ret && ret != Qnil && ret != Qfalse;
// }
//
// static void iodine_ws_write_each_complete(ws_s *ws, void *block) {
//   (void)ws;
//   if ((VALUE)block != Qnil)
//     Registry.remove((VALUE)block);
// }

/**
 * Writes data to all the Websocket connections sharing the same process
 * (worker) except `self`.
 *
 * If a block is given, it will be passed each Websocket connection in turn
 * (much like `each`) and send the data only if the block returns a "truthy"
 * value (i.e. NOT `false` or `nil`).
 *
 * See both {#write} and {#each} for more details.
 */
// static VALUE iodine_ws_multiwrite(VALUE self, VALUE data) {
//   Check_Type(data, T_STRING);
//   ws_s *ws = get_ws(self);
//   // if ((void *)ws == (void *)0x04 || (void *)data == (void *)0x04 ||
//   //     RSTRING_PTR(data) == (void *)0x04)
//   //   fprintf(stderr, "iodine_ws_write: self = %p ; data = %p\n"
//   //                   "\t\tString ptr: %p, String length: %lu\n",
//   //           (void *)ws, (void *)data, RSTRING_PTR(data),
//   RSTRING_LEN(data)); if (!ws || ((protocol_s *)ws)->service !=
//   WEBSOCKET_ID_STR)
//     ws = NULL;
//
//   VALUE block = Qnil;
//   if (rb_block_given_p())
//     block = rb_block_proc();
//   if (block != Qnil)
//     Registry.add(block);
//   websocket_write_each(.origin = ws, .data = RSTRING_PTR(data),
//                        .length = RSTRING_LEN(data),
//                        .is_text = (rb_enc_get(data) == IodineUTF8Encoding),
//                        .on_finished = iodine_ws_write_each_complete,
//                        .filter =
//                            ((block == Qnil) ? NULL : iodine_ws_if_callback),
//                        .arg = (void *)block);
//   return Qtrue;
// }

/* *****************************************************************************
Websocket task performance
*/

static void iodine_ws_perform_each_task(intptr_t fd, protocol_s *protocol,
                                        void *data) {
  (void)(fd);
  VALUE handler = get_handler(protocol);
  if (handler)
    RubyCaller.call2((VALUE)data, iodine_call_proc_id, 1, &handler);
}
static void iodine_ws_finish_each_task(intptr_t fd, void *data) {
  (void)(fd);
  Registry.remove((VALUE)data);
}

inline static void iodine_ws_run_each(intptr_t origin, VALUE block) {
  facil_each(.origin = origin, .service = WEBSOCKET_ID_STR,
             .task = iodine_ws_perform_each_task, .arg = (void *)block,
             .on_complete = iodine_ws_finish_each_task);
}

/** Performs a block of code for each websocket connection. The function returns
the block of code.

The block of code should accept a single variable which is the websocket
connection.

i.e.:

      def on_message data
        msg = data.dup; # data will be overwritten once the function exists.
        each {|ws| ws.write msg}
      end


The block of code will be executed asynchronously, to avoid having two blocks
of code running at the same time and minimizing race conditions when using
multilple threads.
 */
static VALUE iodine_ws_each(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qnil;
  Registry.add(block);
  intptr_t fd = get_uuid(self);
  iodine_ws_run_each(fd, block);
  return block;
}

/**
Runs the required block for each websocket.

Tasks will be performed asynchronously, within each connection's lock, so no
connection will have more then one task being performed at the same time
(similar to {#defer}).

Also, unlike {Iodine.run}, the block will **not** be called unless the
websocket is still open at the time it's execution begins.

Always returns `self`.
*/
static VALUE iodine_ws_class_each(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);
  iodine_ws_run_each(-1, block);
  return self;
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

//////////////////////////////////////
// Protocol functions
void ws_on_open(ws_s *ws) {
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  set_ws(handler, ws);
  RubyCaller.call(handler, iodine_on_open_func_id);
}
void ws_on_close(ws_s *ws) {
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, iodine_on_close_func_id);
  set_ws(handler, Qnil);
  Registry.remove(handler);
}
void ws_on_shutdown(ws_s *ws) {
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, iodine_on_shutdown_func_id);
}
void ws_on_ready(ws_s *ws) {
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  RubyCaller.call(handler, iodine_on_ready_func_id);
}
void ws_on_data(ws_s *ws, char *data, size_t length, uint8_t is_text) {
  (void)(data);
  VALUE handler = get_handler(ws);
  if (!handler)
    return;
  VALUE buffer = rb_ivar_get(handler, iodine_buff_var_id);
  if (is_text)
    rb_enc_associate(buffer, IodineUTF8Encoding);
  else
    rb_enc_associate(buffer, IodineBinaryEncoding);
  rb_str_set_len(buffer, length);
  RubyCaller.call2(handler, iodine_on_message_func_id, 1, &buffer);
}

//////////////
// Empty callbacks for default implementations.

/**  Please implement your own callback for this event.
 */
static VALUE empty_func(VALUE self) {
  (void)(self);
  return Qnil;
}
// /* The `on_message(data)` callback is the main method for any websocket
// implementation. It is the only required callback for a websocket handler
// (without this handler, errors will occur).
//
// <b>NOTICE</b>: the data passed to the `on_message` callback is the actual
// recycble network buffer, not a copy! <b>Use `data.dup` before moving the data
// out of the function's scope</b> to prevent data corruption (i.e. when
// using the data within an `each` block). For example (broadcasting):
//
//       def on_message data
//         msg = data.dup; # data will be overwritten once the function exists.
//         each {|ws| ws.write msg}
//       end
//
// Please override this method and implement your own callback.
// */
// static VALUE def_dyn_message(VALUE self, VALUE data) {
//   fprintf(stderr,
//           "WARNING: websocket handler on_message override missing or "
//           "bypassed.\n");
//   return Qnil;
// }

/* *****************************************************************************
Upgrading
***************************************************************************** */

void iodine_websocket_upgrade(http_request_s *request,
                              http_response_s *response, VALUE handler,
                              size_t max_msg, uint8_t ping) {
  // make sure we have a valid handler, with the Websocket Protocol mixin.
  if (handler == Qnil || handler == Qfalse || TYPE(handler) == T_FIXNUM ||
      TYPE(handler) == T_STRING || TYPE(handler) == T_SYMBOL)
    goto failed;
  if (TYPE(handler) == T_CLASS || TYPE(handler) == T_MODULE) {
    // include the Protocol module
    rb_include_module(handler, IodineWebsocket);
    rb_extend_object(handler, IodineWebsocket);
    handler = RubyCaller.call(handler, iodine_new_func_id);
    if (handler == Qnil || handler == Qfalse)
      goto failed;
    // check that we created a handler
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    rb_include_module(p_class, IodineWebsocket);
    rb_extend_object(p_class, IodineWebsocket);
  }
  // add the handler to the registry
  Registry.add(handler);
  // set the UUID for the connection
  set_uuid(handler, request);
  // send upgrade response and set new protocol
  websocket_upgrade(.request = request, .response = response,
                    .udata = (void *)handler, .on_close = ws_on_close,
                    .on_open = ws_on_open, .on_shutdown = ws_on_shutdown,
                    .on_ready = ws_on_ready, .on_message = ws_on_data,
                    .max_msg_size = max_msg, .timeout = ping);
  return;
failed:
  response->status = 400;
  http_response_finish(response);
  return;
}

/* *****************************************************************************
Initialization
***************************************************************************** */

void Iodine_init_websocket(void) {
  // get IDs and data that's used often
  ws_var_id = rb_intern("iodine_ws_ptr"); // when upgrading
  dup_func_id = rb_intern("dup");         // when upgrading

  force_var_id = ID2SYM(rb_intern("fource"));
  channel_var_id = ID2SYM(rb_intern("channel"));
  pattern_var_id = ID2SYM(rb_intern("pattern"));
  message_var_id = ID2SYM(rb_intern("message"));
  engine_var_id = ID2SYM(rb_intern("engine"));
  text_var_id = ID2SYM(rb_intern("text"));
  binary_var_id = ID2SYM(rb_intern("binary"));

  // the Ruby websockets protocol class.
  IodineWebsocket = rb_define_module_under(Iodine, "Websocket");
  if (IodineWebsocket == Qfalse)
    fprintf(stderr, "WTF?!\n"), exit(-1);
  // // callbacks and handlers
  rb_define_method(IodineWebsocket, "on_open", empty_func, 0);
  // rb_define_method(IodineWebsocket, "on_message", def_dyn_message, 1);
  rb_define_method(IodineWebsocket, "on_shutdown", empty_func, 0);
  rb_define_method(IodineWebsocket, "on_close", empty_func, 0);
  rb_define_method(IodineWebsocket, "on_ready", empty_func, 0);
  rb_define_method(IodineWebsocket, "write", iodine_ws_write, 1);
  rb_define_method(IodineWebsocket, "close", iodine_ws_close, 0);

  rb_define_method(IodineWebsocket, "conn_id", iodine_ws_uuid, 0);
  rb_define_method(IodineWebsocket, "has_pending?", iodine_ws_has_pending, 0);
  rb_define_method(IodineWebsocket, "defer", iodine_defer, -1);
  rb_define_method(IodineWebsocket, "each", iodine_ws_each, 0);
  rb_define_method(IodineWebsocket, "count", iodine_ws_count, 0);

  rb_define_method(IodineWebsocket, "subscribe", iodine_ws_subscribe, 1);
  rb_define_method(IodineWebsocket, "unsubscribe", iodine_ws_unsubscribe, 1);
  rb_define_method(IodineWebsocket, "subscribed?", iodine_ws_is_subscribed, 1);
  rb_define_method(IodineWebsocket, "publish", iodine_ws_publish, 1);

  rb_define_singleton_method(IodineWebsocket, "each", iodine_ws_class_each, 0);
  rb_define_singleton_method(IodineWebsocket, "defer", iodine_class_defer, 1);
  // rb_define_singleton_method(IodineWebsocket, "publish", iodine_ws_publish,
  // 1);
}
