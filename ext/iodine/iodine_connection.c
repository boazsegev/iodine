#include "iodine_connection.h"

#define FIO_INCLUDE_LINKED_LIST
#define FIO_INCLUDE_STR
#include "fio.h"

#include "fiobj4fio.h"
#include "websockets.h"

#include <ruby/io.h>

/* *****************************************************************************
Constants in use
***************************************************************************** */

static ID new_id;
static ID call_id;
static ID to_id;
static ID channel_id;
static ID as_id;
static ID binary_id;
static ID match_id;
static ID redis_id;
static ID handler_id;
static ID engine_id;
static ID message_id;
static ID on_open_id;
static ID on_message_id;
static ID on_drained_id;
static ID ping_id;
static ID on_shutdown_id;
static ID on_close_id;
static VALUE ConnectionKlass;
static rb_encoding *IodineUTF8Encoding;
static VALUE WebSocketSymbol;
static VALUE SSESymbol;
static VALUE RAWSymbol;

/* *****************************************************************************
Pub/Sub storage
***************************************************************************** */

#define FIO_SET_NAME fio_subhash
#define FIO_SET_OBJ_TYPE subscription_s *
#define FIO_SET_KEY_TYPE fio_str_info_s
#define FIO_SET_KEY_COMPARE(s1, s2)                                            \
  ((s1).len == (s2).len &&                                                     \
   ((s1).data == (s2).data || !memcmp((s1).data, (s2).data, (s1).len)))
#define FIO_SET_OBJ_DESTROY(obj) fio_unsubscribe((obj))
#include <fio.h> // creates the fio_str_set_s Set and functions

static inline VALUE iodine_sub_unsubscribe(fio_subhash_s *store,
                                           fio_str_info_s channel) {
  if (fio_subhash_remove(store, fiobj_hash_string(channel.data, channel.len),
                         channel, NULL))
    return Qfalse;
  return Qtrue;
}
static inline void iodine_sub_add(fio_subhash_s *store, subscription_s *sub) {
  fio_str_info_s ch = fio_subscription_channel(sub);
  fio_subhash_insert(store, fiobj_hash_string(ch.data, ch.len), ch, sub, NULL);
}
static inline void iodine_sub_clear_all(fio_subhash_s *store) {
  fio_subhash_free(store);
}

static fio_lock_i sub_lock = FIO_LOCK_INIT;
static fio_subhash_s sub_global = FIO_SET_INIT;

/* *****************************************************************************
C <=> Ruby Data allocation
***************************************************************************** */

typedef struct {
  iodine_connection_s info;
  size_t ref;
  fio_subhash_s subscriptions;
  fio_lock_i lock;
  uint8_t answers_on_message;
  uint8_t answers_on_drained;
  uint8_t answers_ping;
  /* these are one-shot, but the CPU cache might have the data, so set it */
  uint8_t answers_on_open;
  uint8_t answers_on_shutdown;
  uint8_t answers_on_close;
} iodine_connection_data_s;

/** frees an iodine_connection_data_s object*/

/* a callback for the GC (marking active objects) */
static void iodine_connection_data_mark(void *c_) {
  iodine_connection_data_s *c = c_;
  if (!c) {
    return;
  }
  if (c->info.handler && c->info.handler != Qnil) {
    rb_gc_mark(c->info.handler);
  }
  if (c->info.env && c->info.env != Qnil) {
    rb_gc_mark(c->info.env);
  }
}
/* a callback for the GC (freeing inactive objects) */
static void iodine_connection_data_free(void *c_) {
  iodine_connection_data_s *data = c_;
  if (fio_atomic_sub(&data->ref, 1))
    return;
  free(data);
}

static size_t iodine_connection_data_size(const void *c_) {
  return sizeof(iodine_connection_data_s);
  (void)c_;
}

const rb_data_type_t iodine_connection_data_type = {
    .wrap_struct_name = "IodineConnectionData",
    .function =
        {
            .dmark = iodine_connection_data_mark,
            .dfree = iodine_connection_data_free,
            .dsize = iodine_connection_data_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/* Iodine::PubSub::Engine.allocate */
static VALUE iodine_connection_data_alloc_c(VALUE self) {
  iodine_connection_data_s *c = malloc(sizeof(*c));
  *c = (iodine_connection_data_s){
      .info.handler = (VALUE)0,
      .info.uuid = -1,
      .ref = 1,
      .subscriptions = FIO_SET_INIT,
      .lock = FIO_LOCK_INIT,
  };
  return TypedData_Wrap_Struct(self, &iodine_connection_data_type, c);
}

static inline iodine_connection_data_s *iodine_connection_ruby2C(VALUE self) {
  iodine_connection_data_s *c = NULL;
  TypedData_Get_Struct(self, iodine_connection_data_s,
                       &iodine_connection_data_type, c);
  return c;
}

static inline iodine_connection_data_s *
iodine_connection_validate_data(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_ruby2C(self);
  if (c == NULL || c->info.handler == Qnil || c->info.uuid == -1) {
    return NULL;
  }
  return c;
}

/* *****************************************************************************
Ruby Connection Methods - write, close open? pending
***************************************************************************** */

/**
 * Writes data to the connection asynchronously. `data` MUST be a String.
 *
 * In effect, the `write` call does nothing, it only schedules the data to be
 * sent and marks the data as pending.
 *
 * Use {pending} to test how many `write` operations are pending completion
 * (`on_drained(client)` will be called when they complete).
 */
static VALUE iodine_connection_write(VALUE self, VALUE data) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (!c || fio_is_closed(c->info.uuid)) {
    // don't throw exceptions - closed connections are unavoidable.
    return Qnil;
    // rb_raise(rb_eIOError, "Connection closed or invalid.");
  }
  if (!RB_TYPE_P(data, T_STRING)) {
    VALUE tmp = data;
    data = IodineCaller.call(data, iodine_to_s_id);
    if (!RB_TYPE_P(data, T_STRING))
      Check_Type(tmp, T_STRING);
    rb_backtrace();
    FIO_LOG_WARNING(
        "`Iodine::Connection#write` was called with a non-String object.");
  }

  switch (c->info.type) {
  case IODINE_CONNECTION_WEBSOCKET:
    /* WebSockets*/
    websocket_write(c->info.arg, IODINE_RSTRINFO(data),
                    rb_enc_get(data) == IodineUTF8Encoding);
    return Qtrue;
    break;
  case IODINE_CONNECTION_SSE:
/* SSE */
#if 1
    http_sse_write(c->info.arg, .data = IODINE_RSTRINFO(data));
    return Qtrue;
#else
    if (rb_enc_get(data) == IodineUTF8Encoding) {
      http_sse_write(c->info.arg, .data = {.data = RSTRING_PTR(data),
                                           .len = RSTRING_LEN(data)});
      return Qtrue;
    }
    fprintf(stderr, "WARNING: ignoring an attept to write binary data to "
                    "non-binary protocol (SSE).\n");
    return Qfalse;
// rb_raise(rb_eEncodingError,
//          "This Connection type requires data to be UTF-8 encoded.");
#endif
    break;
  case IODINE_CONNECTION_RAW: /* fallthrough */
  default: {
    fio_write(c->info.uuid, RSTRING_PTR(data), RSTRING_LEN(data));
    return Qtrue;
  } break;
  }
  return Qnil;
}

/**
 * Schedules the connection to be closed.
 *
 * The connection will be closed once all the scheduled `write` operations have
 * been completed (or failed).
 */
static VALUE iodine_connection_close(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c && !fio_is_closed(c->info.uuid)) {
    if (c->info.type == IODINE_CONNECTION_WEBSOCKET) {
      websocket_close(c->info.arg); /* sends WebSocket close packet */
    } else {
      fio_close(c->info.uuid);
    }
  }

  return Qnil;
}
/** Returns true if the connection appears to be open (no known issues). */
static VALUE iodine_connection_is_open(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c && !fio_is_closed(c->info.uuid)) {
    return Qtrue;
  }
  return Qfalse;
}

/**
 * Always returns true, since Iodine connections support the pub/sub extension.
 */
static VALUE iodine_connection_is_pubsub(VALUE self) {
  return INT2NUM(0);
  (void)self;
}
/**
 * Returns the number of pending `write` operations that need to complete
 * before the next `on_drained` callback is called.
 *
 * Returns -1 if the connection is closed and 0 if `on_drained` won't be
 * scheduled (no pending `write`).
 */
static VALUE iodine_connection_pending(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (!c || fio_is_closed(c->info.uuid)) {
    return INT2NUM(-1);
  }
  return SIZET2NUM((fio_pending(c->info.uuid)));
}

// clang-format off
/**
 * Returns the connection's protocol Symbol (`:sse`, `:websocket` or `:raw`).
 *
 * @Note For compatibility reasons (with ther `rack.upgrade` servers), it might be more prudent to use the data in the {#env} (`env['rack.upgrade?']`). However, this method is provided both as a faster alternative and for those cases where Raw / Custom (TCP/IP) data stream is a valid option.
*/
static VALUE iodine_connection_protocol_name(VALUE self) {
  // clang-format on
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c) {
    switch (c->info.type) {
    case IODINE_CONNECTION_WEBSOCKET:
      return WebSocketSymbol;
      break;
    case IODINE_CONNECTION_SSE:
      return SSESymbol;
      break;
    case IODINE_CONNECTION_RAW: /* fallthrough */
      return RAWSymbol;
      break;
    }
  }
  return Qnil;
}

/**
 * Returns the timeout / `ping` interval for the connection.
 *
 * Returns nil on error.
 */
static VALUE iodine_connection_timeout_get(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c && !fio_is_closed(c->info.uuid)) {
    size_t tout = (size_t)fio_timeout_get(c->info.uuid);
    return SIZET2NUM(tout);
  }
  return Qnil;
}

/**
 * Sets the timeout / `ping` interval for the connection (up to 255 seconds).
 *
 * Returns nil on error.
 */
static VALUE iodine_connection_timeout_set(VALUE self, VALUE timeout) {
  Check_Type(timeout, T_FIXNUM);
  int tout = NUM2INT(timeout);
  if (tout < 0 || tout > 255) {
    rb_raise(rb_eRangeError, "timeout out of range.");
    return Qnil;
  }
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c && !fio_is_closed(c->info.uuid)) {
    fio_timeout_set(c->info.uuid, (uint8_t)tout);
    return timeout;
  }
  return Qnil;
}

/**
 * Returns the connection's `env` (if it originated from an HTTP request).
 */
static VALUE iodine_connection_env(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c && c->info.env) {
    return c->info.env;
  }
  return Qnil;
}

/**
 * Returns the client's current callback object.
 */
static VALUE iodine_connection_handler_get(VALUE self) {
  iodine_connection_data_s *data = iodine_connection_validate_data(self);
  if (!data) {
    FIO_LOG_DEBUG("(iodine) requested connection handler for "
                  "an invalid connection: %p",
                  (void *)self);
    return Qnil;
  }
  return data->info.handler;
}

// clang-format off
/**
 * Sets the client's callback object, so future events will use the new object's callbacks.
 *
 * @Note this will fire the `on_close` callback in the old handler and the `on_open` callback on the new handler. However, existing subscriptions will remain intact.
 */
static VALUE iodine_connection_handler_set(VALUE self, VALUE handler) {
  // clang-format on
  iodine_connection_data_s *data = iodine_connection_validate_data(self);
  if (!data) {
    FIO_LOG_DEBUG("(iodine) attempted to set a connection handler for "
                  "an invalid connection: %p",
                  (void *)self);
    return Qnil;
  }
  if (handler == Qnil || handler == Qfalse) {
    FIO_LOG_DEBUG(
        "(iodine) called client.handler = nil, closing connection: %p",
        (void *)self);
    iodine_connection_close(self);
    return Qnil;
  }
  if (data->info.handler != handler) {
    uint8_t answers_on_open = (rb_respond_to(handler, on_open_id) != 0);
    if (data->answers_on_close)
      IodineCaller.call2(data->info.handler, on_close_id, 1, &self);
    fio_lock(&data->lock);
    data->info.handler = handler;
    data->answers_on_open = answers_on_open,
    data->answers_on_message = (rb_respond_to(handler, on_message_id) != 0),
    data->answers_ping = (rb_respond_to(handler, ping_id) != 0),
    data->answers_on_drained = (rb_respond_to(handler, on_drained_id) != 0),
    data->answers_on_shutdown = (rb_respond_to(handler, on_shutdown_id) != 0),
    data->answers_on_close = (rb_respond_to(handler, on_close_id) != 0),
    fio_unlock(&data->lock);
    if (answers_on_open) {
      iodine_connection_fire_event(self, IODINE_CONNECTION_ON_OPEN, Qnil);
    }
    FIO_LOG_DEBUG("(iodine) switched handlers for connection: %p",
                  (void *)self);
  }
  return handler;
}
/* *****************************************************************************
Pub/Sub Callbacks (internal implementation)
***************************************************************************** */

/* calls the Ruby block assigned to a pubsub event (within the GVL). */
static void *iodine_on_pubsub_call_block(void *msg_) {
  fio_msg_s *msg = msg_;
  VALUE args[2];
  args[0] = rb_str_new(msg->channel.data, msg->channel.len);
  IodineStore.add(args[0]);
  args[1] = rb_str_new(msg->msg.data, msg->msg.len);
  IodineStore.add(args[1]);
  IodineCaller.call2((VALUE)msg->udata2, call_id, 2, args);
  IodineStore.remove(args[1]);
  IodineStore.remove(args[0]);
  return NULL;
}

/* callback for incoming subscription messages */
static void iodine_on_pubsub(fio_msg_s *msg) {
  iodine_connection_data_s *data = msg->udata1;
  VALUE block = (VALUE)msg->udata2;
  switch (block) {
  case 0:       /* fallthrough */
  case Qnil:    /* fallthrough */
  case Qtrue: { /* Qtrue == binary WebSocket */
    if (!data) {
      FIO_LOG_ERROR("Pub/Sub direct called with no connection data!");
      return;
    }
    if (data->info.handler == Qnil || data->info.uuid == -1 ||
        fio_is_closed(data->info.uuid))
      return;
    switch (data->info.type) {
    case IODINE_CONNECTION_WEBSOCKET: {
      FIOBJ s = (FIOBJ)fio_message_metadata(
          msg, (block == Qnil ? WEBSOCKET_OPTIMIZE_PUBSUB
                              : WEBSOCKET_OPTIMIZE_PUBSUB_BINARY));
      if (s) {
        // fwrite(".", 1, 1, stderr);
        fiobj_send_free(data->info.uuid, fiobj_dup(s));
      } else {
        fwrite("-", 1, 1, stderr);
        websocket_write(data->info.arg, msg->msg, (block == Qnil));
      }
      return;
    }
    case IODINE_CONNECTION_SSE:
      http_sse_write(data->info.arg, .data = msg->msg);
      return;
    default:
      fio_write(data->info.uuid, msg->msg.data, msg->msg.len);
      return;
    }
  }
  default:
    if (data && data->info.uuid != -1) {
      fio_protocol_s *pr =
          fio_protocol_try_lock(data->info.uuid, FIO_PR_LOCK_TASK);
      if (!pr) {
        // perror("Connection lock failed");
        if (errno != EBADF)
          fio_message_defer(msg);
        break;
      }
      IodineCaller.enterGVL(iodine_on_pubsub_call_block, msg);
      fio_protocol_unlock(pr, FIO_PR_LOCK_TASK);
    } else {
      IodineCaller.enterGVL(iodine_on_pubsub_call_block, msg);
    }
    break;
  }
}

/* callback for subscription closure */
static void iodine_on_unsubscribe(void *udata1, void *udata2) {
  iodine_connection_data_s *data = udata1;
  VALUE block = (VALUE)udata2;
  switch (block) {
  case Qnil:
    if (data && data->info.type == IODINE_CONNECTION_WEBSOCKET) {
      websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB, 0);
    }
    break;
  case Qtrue:
    if (data && data->info.type == IODINE_CONNECTION_WEBSOCKET) {
      websocket_optimize4broadcasts(WEBSOCKET_OPTIMIZE_PUBSUB_BINARY, 0);
    }
    break;
  default:
    IodineStore.remove(block);
    break;
  }
  if (data) {
    iodine_connection_data_free(data);
  }
}

/* *****************************************************************************
Ruby Connection Methods - Pub/Sub
***************************************************************************** */

typedef struct {
  VALUE channel;
  VALUE block;
  fio_match_fn pattern;
  uint8_t binary;
} iodine_sub_args_s;

/** Tests the `subscribe` Ruby arguments */
static iodine_sub_args_s iodine_subscribe_args(int argc, VALUE *argv) {

  iodine_sub_args_s ret = {.channel = Qnil, .block = Qnil};
  VALUE rb_opt = 0;

  switch (argc) {
  case 2:
    ret.channel = argv[0];
    rb_opt = argv[1];
    break;
  case 1:
    /* single argument must be a Hash / channel name */
    if (TYPE(argv[0]) == T_HASH) {
      rb_opt = argv[0];
      ret.channel = rb_hash_aref(argv[0], to_id);
      if (ret.channel == Qnil || ret.channel == Qfalse) {
        /* temporary backport support */
        ret.channel = rb_hash_aref(argv[0], channel_id);
        if (ret.channel != Qnil) {
          FIO_LOG_WARNING("use of :channel in subscribe is deprecated.");
        }
      }
    } else {
      ret.channel = argv[0];
    }
    break;
  default:
    rb_raise(rb_eArgError, "method accepts 1 or 2 arguments.");
    return ret;
  }

  if (ret.channel == Qnil || ret.channel == Qfalse) {
    rb_raise(rb_eArgError,
             "a target (:to) subject / stream / channel is required.");
    return ret;
  }

  if (TYPE(ret.channel) == T_SYMBOL)
    ret.channel = rb_sym2str(ret.channel);
  Check_Type(ret.channel, T_STRING);

  if (rb_opt) {
    if (rb_hash_aref(rb_opt, as_id) == binary_id) {
      ret.binary = 1;
    }
    if (rb_hash_aref(rb_opt, match_id) == redis_id) {
      ret.pattern = FIO_MATCH_GLOB;
    }
    ret.block = rb_hash_aref(rb_opt, handler_id);
    if (ret.block != Qnil) {
      IodineStore.add(ret.block);
    }
  }

  if (ret.block == Qnil) {
    if (rb_block_given_p()) {
      ret.block = rb_block_proc();
      IodineStore.add(ret.block);
    }
  }
  return ret;
}

// clang-format off
/**
Subscribes to a Pub/Sub stream / channel or replaces an existing subscription.

The method accepts 1-2 arguments and an optional block. These are all valid ways
to call the method:

      subscribe("my_stream") {|source, msg| p msg }
      subscribe("my_strea*", match: :redis) {|source, msg| p msg }
      subscribe(to: "my_stream")  {|source, msg| p msg }
      # or use any object that answers `#call(source, msg)`
      MyProc = Proc.new {|source, msg| p msg }
      subscribe to: "my_stream", match: :redis, handler: MyProc

The first argument must be either a String or a Hash.

The second, optional, argument must be a Hash (if given).

The options Hash supports the following possible keys (other keys are ignored, all keys are Symbols):

- `:match` - The channel / subject name matching type to be used. Valid value is: `:redis`. Future versions hope to support `:nats` and `:rabbit` patern matching as well.
- `:to` - The channel / subject to subscribe to.
- `:as` - (only for WebSocket connections) accepts the optional value `:binary`. default is `:text`. Note that binary transmissions are illegal for some connections (such as SSE) and an attempted binary subscription will fail for these connections.
- `:handler` - Any object that answers `.call(source, msg)` where source is the stream / channel name.

Note: if an existing subscription with the same name exists, it will be replaced by this new subscription.

Returns the name of the subscription, which matches the name be used in {unsubscribe} (or nil on failure).

*/
static VALUE iodine_pubsub_subscribe(int argc, VALUE *argv, VALUE self) {
  // clang-format on
  iodine_sub_args_s args = iodine_subscribe_args(argc, argv);
  if (args.channel == Qnil) {
    return Qnil;
  }
  iodine_connection_data_s *c = NULL;
  if (TYPE(self) == T_MODULE) {
    if (!args.block) {
      rb_raise(rb_eArgError,
               "block or :handler required for local subscriptions.");
    }
  } else {
    c = iodine_connection_validate_data(self);
    if (!c || (c->info.type == IODINE_CONNECTION_SSE && args.binary)) {
      if (args.block) {
        IodineStore.remove(args.block);
      }
      return Qnil; /* cannot subscribe a closed / invalid connection. */
    }
    if (args.block == Qnil) {
      if (c->info.type == IODINE_CONNECTION_WEBSOCKET)
        websocket_optimize4broadcasts((args.binary
                                           ? WEBSOCKET_OPTIMIZE_PUBSUB_BINARY
                                           : WEBSOCKET_OPTIMIZE_PUBSUB),
                                      1);
      if (args.binary) {
        args.block = Qtrue;
      }
    }
    fio_atomic_add(&c->ref, 1);
  }

  subscription_s *sub =
      fio_subscribe(.channel = IODINE_RSTRINFO(args.channel),
                    .on_message = iodine_on_pubsub,
                    .on_unsubscribe = iodine_on_unsubscribe, .udata1 = c,
                    .udata2 = (void *)args.block, .match = args.pattern);
  if (c) {
    fio_lock(&c->lock);
    if (c->info.uuid == -1) {
      fio_unsubscribe(sub);
      fio_unlock(&c->lock);
      return Qnil;
    }
    iodine_sub_add(&c->subscriptions, sub);
    fio_unlock(&c->lock);
  } else {
    fio_lock(&sub_lock);
    iodine_sub_add(&sub_global, sub);
    fio_unlock(&sub_lock);
  }
  return args.channel;
}

// clang-format off
/**
Unsubscribes from a Pub/Sub stream / channel.

The method accepts a single arguments, the name used for the subscription. i.e.:

      subscribe("my_stream") {|source, msg| p msg }
      unsubscribe("my_stream")

Returns `true` if the subscription was found.

Returns `false` if the subscription didn't exist.
*/
static VALUE iodine_pubsub_unsubscribe(VALUE self, VALUE name) {
  // clang-format on
  iodine_connection_data_s *c = NULL;
  fio_lock_i *s_lock = &sub_lock;
  fio_subhash_s *subs = &sub_global;
  VALUE ret;
  if (TYPE(self) != T_MODULE) {
    c = iodine_connection_validate_data(self);
    if (!c || c->info.uuid == -1) {
      return Qnil; /* cannot unsubscribe a closed connection. */
    }
    s_lock = &c->lock;
    subs = &c->subscriptions;
  }
  fio_lock(s_lock);
  ret = iodine_sub_unsubscribe(subs, IODINE_RSTRINFO(name));
  fio_unlock(s_lock);
  return ret;
}

// clang-format off
/**
Publishes a message to a channel.

Can be used using two Strings:

      publish(to, message)

The method accepts an optional `engine` argument:

      publish(to, message, my_pubsub_engine)

*/
static VALUE iodine_pubsub_publish(int argc, VALUE *argv, VALUE self) {
  // clang-format on
  VALUE rb_ch, rb_msg, rb_engine = Qnil;
  const fio_pubsub_engine_s *engine = NULL;
  switch (argc) {
  case 3:
    /* fallthrough */
    rb_engine = argv[2];
  case 2:
    rb_ch = argv[0];
    rb_msg = argv[1];
    break;
  case 1: {
    /* single argument must be a Hash */
    Check_Type(argv[0], T_HASH);
    rb_ch = rb_hash_aref(argv[0], to_id);
    if (rb_ch == Qnil || rb_ch == Qfalse) {
      rb_ch = rb_hash_aref(argv[0], channel_id);
    }
    rb_msg = rb_hash_aref(argv[0], message_id);
    rb_engine = rb_hash_aref(argv[0], engine_id);
  } break;
  default:
    rb_raise(rb_eArgError, "method accepts 1-3 arguments.");
  }

  if (rb_msg == Qnil || rb_msg == Qfalse) {
    rb_raise(rb_eArgError, "message is required.");
  }
  Check_Type(rb_msg, T_STRING);

  if (rb_ch == Qnil || rb_ch == Qfalse)
    rb_raise(rb_eArgError, "target / channel is required .");
  if (TYPE(rb_ch) == T_SYMBOL)
    rb_ch = rb_sym2str(rb_ch);
  Check_Type(rb_ch, T_STRING);

  if (rb_engine == Qfalse) {
    engine = FIO_PUBSUB_PROCESS;
  } else if (rb_engine != Qnil) {
    // collect engine object
    iodine_pubsub_s *e = iodine_pubsub_CData(rb_engine);
    if (e) {
      engine = e->engine;
    }
  }

  fio_publish(.engine = engine, .channel = IODINE_RSTRINFO(rb_ch),
              .message = IODINE_RSTRINFO(rb_msg));
  return Qtrue;
  (void)self;
}

/* *****************************************************************************
Published C functions
***************************************************************************** */

#undef iodine_connection_new
VALUE iodine_connection_new(iodine_connection_s args) {
  VALUE connection = IodineCaller.call(ConnectionKlass, new_id);
  if (connection == Qnil) {
    return Qnil;
  }
  IodineStore.add(connection);
  iodine_connection_data_s *data = iodine_connection_ruby2C(connection);
  if (data == NULL) {
    FIO_LOG_ERROR("(iodine) internal error, connection object has no C data!");
    return Qnil;
  }
  *data = (iodine_connection_data_s){
      .info = args,
      .subscriptions = FIO_SET_INIT,
      .ref = 1,
      .answers_on_open = (rb_respond_to(args.handler, on_open_id) != 0),
      .answers_on_message = (rb_respond_to(args.handler, on_message_id) != 0),
      .answers_ping = (rb_respond_to(args.handler, ping_id) != 0),
      .answers_on_drained = (rb_respond_to(args.handler, on_drained_id) != 0),
      .answers_on_shutdown = (rb_respond_to(args.handler, on_shutdown_id) != 0),
      .answers_on_close = (rb_respond_to(args.handler, on_close_id) != 0),
      .lock = FIO_LOCK_INIT,
  };
  return connection;
}

/** Fires a connection object's event */
void iodine_connection_fire_event(VALUE connection,
                                  iodine_connection_event_type_e ev,
                                  VALUE msg) {
  if (!connection || connection == Qnil) {
    FIO_LOG_ERROR(
        "(iodine) nil connection handle used by an internal API call");
    return;
  }
  iodine_connection_data_s *data = iodine_connection_validate_data(connection);
  if (!data) {
    FIO_LOG_ERROR("(iodine) invalid connection handle used by an "
                  "internal API call: %p",
                  (void *)connection);
    return;
  }
  if (!data->info.handler || data->info.handler == Qnil) {
    FIO_LOG_DEBUG("(iodine) invalid connection handler, can't fire event %d",
                  (int)ev);
    return;
  }
  VALUE args[2] = {connection, msg};
  switch (ev) {
  case IODINE_CONNECTION_ON_OPEN:
    if (data->answers_on_open) {
      IodineCaller.call2(data->info.handler, on_open_id, 1, args);
    }
    break;
  case IODINE_CONNECTION_ON_MESSAGE:
    if (data->answers_on_message) {
      IodineCaller.call2(data->info.handler, on_message_id, 2, args);
    }
    break;
  case IODINE_CONNECTION_ON_DRAINED:
    if (data->answers_on_drained) {
      IodineCaller.call2(data->info.handler, on_drained_id, 1, args);
    }
    break;
  case IODINE_CONNECTION_ON_SHUTDOWN:
    if (data->answers_on_shutdown) {
      IodineCaller.call2(data->info.handler, on_shutdown_id, 1, args);
    }
    break;
  case IODINE_CONNECTION_PING:
    if (data->answers_ping) {
      IodineCaller.call2(data->info.handler, ping_id, 1, args);
    }
    break;

  case IODINE_CONNECTION_ON_CLOSE:
    if (data->answers_on_close) {
      IodineCaller.call2(data->info.handler, on_close_id, 1, args);
    }
    fio_lock(&data->lock);
    iodine_sub_clear_all(&data->subscriptions);
    data->info.handler = Qnil;
    data->info.env = Qnil;
    data->info.uuid = -1;
    data->info.arg = NULL;
    fio_unlock(&data->lock);
    IodineStore.remove(connection);
    break;
  default:
    break;
  }
}

void iodine_connection_init(void) {
  // set used constants
  IodineUTF8Encoding = rb_enc_find("UTF-8");
  // used ID objects
  new_id = rb_intern2("new", 3);
  call_id = rb_intern2("call", 4);

  to_id = rb_intern2("to", 2);
  channel_id = rb_intern2("channel", 7);
  as_id = rb_intern2("as", 2);
  binary_id = rb_intern2("binary", 6);
  match_id = rb_intern2("match", 5);
  redis_id = rb_intern2("redis", 5);
  handler_id = rb_intern2("handler", 7);
  engine_id = rb_intern2("engine", 6);
  message_id = rb_intern2("message", 7);
  on_open_id = rb_intern("on_open");
  on_message_id = rb_intern("on_message");
  on_drained_id = rb_intern("on_drained");
  on_shutdown_id = rb_intern("on_shutdown");
  on_close_id = rb_intern("on_close");
  ping_id = rb_intern("ping");

  // globalize ID objects
  if (1) {
    IodineStore.add(ID2SYM(to_id));
    IodineStore.add(ID2SYM(channel_id));
    IodineStore.add(ID2SYM(as_id));
    IodineStore.add(ID2SYM(binary_id));
    IodineStore.add(ID2SYM(match_id));
    IodineStore.add(ID2SYM(redis_id));
    IodineStore.add(ID2SYM(handler_id));
    IodineStore.add(ID2SYM(engine_id));
    IodineStore.add(ID2SYM(message_id));
    IodineStore.add(ID2SYM(on_open_id));
    IodineStore.add(ID2SYM(on_message_id));
    IodineStore.add(ID2SYM(on_drained_id));
    IodineStore.add(ID2SYM(on_shutdown_id));
    IodineStore.add(ID2SYM(on_close_id));
    IodineStore.add(ID2SYM(ping_id));
  }

  // should these be globalized?
  WebSocketSymbol = ID2SYM(rb_intern("websocket"));
  SSESymbol = ID2SYM(rb_intern("sse"));
  RAWSymbol = ID2SYM(rb_intern("raw"));
  IodineStore.add(WebSocketSymbol);
  IodineStore.add(SSESymbol);
  IodineStore.add(RAWSymbol);

  // define the Connection Class and it's methods
  ConnectionKlass = rb_define_class_under(IodineModule, "Connection", rb_cData);
  rb_define_alloc_func(ConnectionKlass, iodine_connection_data_alloc_c);
  rb_define_method(ConnectionKlass, "write", iodine_connection_write, 1);
  rb_define_method(ConnectionKlass, "close", iodine_connection_close, 0);
  rb_define_method(ConnectionKlass, "open?", iodine_connection_is_open, 0);
  rb_define_method(ConnectionKlass, "pending", iodine_connection_pending, 0);
  rb_define_method(ConnectionKlass, "protocol", iodine_connection_protocol_name,
                   0);
  rb_define_method(ConnectionKlass, "timeout", iodine_connection_timeout_get,
                   0);
  rb_define_method(ConnectionKlass, "timeout=", iodine_connection_timeout_set,
                   1);
  rb_define_method(ConnectionKlass, "env", iodine_connection_env, 0);

  rb_define_method(ConnectionKlass, "handler", iodine_connection_handler_get,
                   0);
  rb_define_method(ConnectionKlass, "handler=", iodine_connection_handler_set,
                   1);
  rb_define_method(ConnectionKlass, "pubsub?", iodine_connection_is_pubsub, 0);
  rb_define_method(ConnectionKlass, "subscribe", iodine_pubsub_subscribe, -1);
  rb_define_method(ConnectionKlass, "unsubscribe", iodine_pubsub_unsubscribe,
                   1);
  rb_define_method(ConnectionKlass, "publish", iodine_pubsub_publish, -1);

  // define global methods
  rb_define_module_function(IodineModule, "subscribe", iodine_pubsub_subscribe,
                            -1);
  rb_define_module_function(IodineModule, "unsubscribe",
                            iodine_pubsub_unsubscribe, 1);
  rb_define_module_function(IodineModule, "publish", iodine_pubsub_publish, -1);
}
