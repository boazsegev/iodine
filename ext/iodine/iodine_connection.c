#include "iodine_connection.h"

#include "facil.h"
#include "fio_mem.h"
#include "fiobj4sock.h"
#include "pubsub.h"
#include "websockets.h"

#include "spnlock.inc"

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
static ID on_closed_id;
static VALUE ConnectionKlass;
static rb_encoding *IodineUTF8Encoding;
static VALUE WebSocketSymbol;
static VALUE SSESymbol;
static VALUE RAWSymbol;

/* *****************************************************************************
Pub/Sub storage
***************************************************************************** */

#define FIO_HASH_KEY_TYPE FIOBJ
#define FIO_HASH_KEY_INVALID FIOBJ_INVALID
#define FIO_HASH_KEY2UINT(key) fiobj_obj2hash((key))
#define FIO_HASH_COMPARE_KEYS(k1, k2) (fiobj_iseq((k1), (k2)))
#define FIO_HASH_KEY_ISINVALID(key) ((key) == FIOBJ_INVALID)
#define FIO_HASH_KEY_COPY(key) (fiobj_dup(key))
#define FIO_HASH_KEY_DESTROY(key) (fiobj_free((key)))

#include "fio_hashmap.h"

static inline VALUE iodine_sub_unsubscribe(fio_hash_s *store, FIOBJ channel) {
  pubsub_sub_pt sub = fio_hash_insert(store, channel, NULL);
  if (sub) {
    pubsub_unsubscribe(sub);
    return Qtrue;
  }
  return Qfalse;
}
static inline void iodine_sub_add(fio_hash_s *store, pubsub_sub_pt sub) {
  FIOBJ channel = pubsub_sub_channel(sub); /* used for memory optimization */
  sub = fio_hash_insert(store, channel, sub);
  if (sub) {
    pubsub_unsubscribe(sub);
  }
}
static inline void iodine_sub_clear_all(fio_hash_s *store) {
  FIO_HASH_FOR_FREE(store, pos) {
    if (pos->obj) {
      pubsub_unsubscribe(pos->obj);
    }
  }
}

static spn_lock_i sub_lock = SPN_LOCK_INIT;
static fio_hash_s sub_global = FIO_HASH_INIT;

/* *****************************************************************************
C <=> Ruby Data allocation
***************************************************************************** */

typedef struct {
  iodine_connection_s info;
  size_t ref;
  fio_hash_s subscriptions;
  spn_lock_i lock;
  uint8_t answers_on_message;
  uint8_t answers_on_drained;
  uint8_t answers_ping;
  /* these are one-shot, but the CPU cache might have the data, so set it */
  uint8_t answers_on_open;
  uint8_t answers_on_shutdown;
  uint8_t answers_on_closed;
} iodine_connection_data_s;

/** frees an iodine_connection_data_s object*/

/* a callback for the GC (marking active objects) */
static void iodine_connection_data_mark(void *c_) {
  iodine_connection_data_s *c = c_;
  if (c->info.handler != Qnil) {
    rb_gc_mark(c->info.handler);
  }
  if (c->info.env && c->info.env != Qnil) {
    rb_gc_mark(c->info.env);
  }
}
/* a callback for the GC (marking active objects) */
static void iodine_connection_data_free(void *c_) {
  iodine_connection_data_s *data = c_;
  if (spn_sub(&data->ref, 1))
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
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/* Iodine::PubSub::Engine.allocate */
static VALUE iodine_connection_data_alloc_c(VALUE self) {
  iodine_connection_data_s *c = malloc(sizeof(*c));
  *c = (iodine_connection_data_s){
      .info.handler = (VALUE)0,
      .info.uuid = -1,
      .ref = 1,
      .subscriptions = FIO_HASH_INIT,
      .lock = SPN_LOCK_INIT,
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
 * Writes data to the connection asynchronously.
 *
 * In effect, the `write` call does nothing, it only schedules the data to be
 * sent and marks the data as pending.
 *
 * Use {pending} to test how many `write` operations are pending completion
 * (`on_drained(client)` will be called when they complete).
 */
static VALUE iodine_connection_write(VALUE self, VALUE data) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (!c || sock_isclosed(c->info.uuid)) {
    rb_raise(rb_eIOError, "Connection closed or invalid.");
  }
  switch (c->info.type) {
  case IODINE_CONNECTION_WEBSOCKET:
    /* WebSockets*/
    websocket_write(c->info.arg, RSTRING_PTR(data), RSTRING_LEN(data),
                    rb_enc_get(data) == IodineUTF8Encoding);
    return Qtrue;
    break;
  case IODINE_CONNECTION_SSE:
/* SSE */
#if 1
    http_sse_write(c->info.arg, .data = {.data = RSTRING_PTR(data),
                                         .len = RSTRING_LEN(data)});
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
    size_t len = RSTRING_LEN(data);
    char *copy = fio_malloc(len);
    if (!copy) {
      rb_raise(rb_eNoMemError, "failed to allocate memory for network buffer!");
    }
    memcpy(copy, RSTRING_PTR(data), len);
    sock_write2(.uuid = c->info.uuid, .buffer = copy, .length = len,
                .dealloc = fio_free);
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
  if (c && !sock_isclosed(c->info.uuid)) {
    if (c->info.type == IODINE_CONNECTION_WEBSOCKET) {
      websocket_close(c->info.arg);
    } else {
      sock_close(c->info.uuid);
    }
  }

  return Qnil;
}
/** Returns true if the connection appears to be open (no known issues). */
static VALUE iodine_connection_is_open(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c && !sock_isclosed(c->info.uuid)) {
    return Qtrue;
  }
  return Qfalse;
}
/**
 * Returns the number of pending `write` operations that need to complete
 * before the next `on_drained` callback is called.
 */
static VALUE iodine_connection_pending(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (!c || sock_isclosed(c->info.uuid)) {
    return INT2NUM(-1);
  }
  return SIZET2NUM((sock_pending(c->info.uuid)));
}

/** Returns the connection's protocol Symbol (`:sse`, `:websocket`, etc'). */
static VALUE iodine_connection_protocol_name(VALUE self) {
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
  if (c && !sock_isclosed(c->info.uuid)) {
    size_t tout = (size_t)facil_get_timeout(c->info.uuid);
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
  if (c && !sock_isclosed(c->info.uuid)) {
    facil_set_timeout(c->info.uuid, (uint8_t)tout);
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

/* *****************************************************************************
Pub/Sub Callbacks (internal implementation)
***************************************************************************** */

/* calls the Ruby block assigned to a pubsub event (within the GVL). */
static void *iodine_on_pubsub_call_block(void *msg_) {
  pubsub_message_s *msg = msg_;
  fio_cstr_s tmp;
  VALUE args[2];
  tmp = fiobj_obj2cstr(msg->channel);
  args[0] = rb_str_new(tmp.data, tmp.len);
  tmp = fiobj_obj2cstr(msg->message);
  args[1] = rb_str_new(tmp.data, tmp.len);
  IodineCaller.call2((VALUE)msg->udata2, call_id, 2, args);
  return NULL;
}

/* callback for incoming subscription messages */
static void iodine_on_pubsub(pubsub_message_s *msg) {
  iodine_connection_data_s *data = msg->udata1;
  VALUE block = (VALUE)msg->udata2;
  switch (block) {
  case Qnil: /* fallthrough */
  case Qtrue: {
    if (data->info.handler == Qnil || data->info.uuid == -1 ||
        sock_isclosed(data->info.uuid))
      return;
    switch (data->info.type) {
    case IODINE_CONNECTION_WEBSOCKET: {
      fio_cstr_s str = fiobj_obj2cstr(msg->message);
      websocket_write(data->info.arg, str.data, str.len, (block == Qnil));
      return;
    }
    case IODINE_CONNECTION_SSE:
      http_sse_write(data->info.arg, .data = fiobj_obj2cstr(msg->message));
      return;
    default:
      fiobj_send_free(data->info.uuid, fiobj_dup(msg->message));
      return;
    }
  }
  default:
    IodineCaller.enterGVL(iodine_on_pubsub_call_block, msg);
    break;
  }
}

/* callback for subscription closure */
static void iodine_on_unsubscribe(void *udata1, void *udata2) {
  iodine_connection_data_s *data = udata1;
  VALUE block = (VALUE)udata2;
  switch (block) {
  case Qnil:
  case Qtrue:
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
  uint8_t binary;
  uint8_t pattern;
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
          fprintf(stderr,
                  "WARNING: use of :channel in subscribe is deprecated.\n");
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
      ret.pattern = 1;
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

:match :: The channel / subject name matching type to be used. Valid value is: `:redis`. Future versions hope to support `:nats` and `:rabbit` patern matching as well.

:to :: The channel / subject to subscribe to.

:as :: (only for WebSocket connections) accepts the optional value `:binary`. default is `:text`. Note that binary transmissions are illegal for some connections (such as SSE) and an attempted binary subscription will fail for these connections.

:handler :: Any object that answers `#call(source, msg)` where source is the stream / channel name.

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
      return Qnil; /* cannot subscribe a closed connection. */
    }
    if (args.block == Qnil && args.binary) {
      args.block = Qtrue;
    }
    spn_add(&c->ref, 1);
  }

  FIOBJ channel =
      fiobj_str_new(RSTRING_PTR(args.channel), RSTRING_LEN(args.channel));
  pubsub_sub_pt sub =
      pubsub_subscribe(.channel = channel, .on_message = iodine_on_pubsub,
                       .on_unsubscribe = iodine_on_unsubscribe, .udata1 = c,
                       .udata2 = (void *)args.block,
                       .use_pattern = args.pattern);
  fiobj_free(channel);
  if (c) {
    spn_lock(&c->lock);
    if (c->info.uuid == -1) {
      pubsub_unsubscribe(sub);
      spn_unlock(&c->lock);
      return Qnil;
    } else {
      iodine_sub_add(&c->subscriptions, sub);
    }
    spn_unlock(&c->lock);
  } else {
    spn_lock(&sub_lock);
    iodine_sub_add(&sub_global, sub);
    spn_unlock(&sub_lock);
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
  FIOBJ channel = fiobj_str_new(RSTRING_PTR(name), RSTRING_LEN(name));
  VALUE ret;
  if (TYPE(self) == T_MODULE) {
    spn_lock(&sub_lock);
    ret = iodine_sub_unsubscribe(&sub_global, channel);
    spn_unlock(&sub_lock);
  } else {
    c = iodine_connection_validate_data(self);
    if (!c) {
      return Qnil; /* cannot subscribe a closed connection. */
    }
    spn_lock(&sub_lock);
    ret = iodine_sub_unsubscribe(&sub_global, channel);
    spn_unlock(&sub_lock);
  }
  fiobj_free(channel);
  return ret;
}

/**
Publishes a message to a channel.

Can be used using two Strings:

      publish(to, message)

The method accepts an optional `engine` argument:

      publish(to, message, my_pubsub_engine)


Alternatively, accepts the following named arguments:

:to :: The channel to publish to (required).

:message :: The message to be published (required).

:engine :: If provided, the engine to use for pub/sub. Otherwise the default
engine is used.

*/
static VALUE iodine_pubsub_publish(int argc, VALUE *argv, VALUE self) {
  VALUE rb_ch, rb_msg, rb_engine = Qnil;
  const pubsub_engine_s *engine = NULL;
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
    engine = PUBSUB_PROCESS_ENGINE;
  } else if (rb_engine != Qnil) {
    // collect engine object
    iodine_pubsub_s *e = iodine_pubsub_CData(rb_engine);
    if (e) {
      engine = e->engine;
    }
  }

  FIOBJ ch = fiobj_str_new(RSTRING_PTR(rb_ch), RSTRING_LEN(rb_ch));
  FIOBJ msg = fiobj_str_new(RSTRING_PTR(rb_msg), RSTRING_LEN(rb_msg));

  intptr_t ret =
      pubsub_publish(.engine = engine, .channel = ch, .message = msg);
  fiobj_free(ch);
  fiobj_free(msg);
  if (!ret)
    return Qfalse;
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
    fprintf(
        stderr,
        "ERROR: (iodine) internal error, connection object has no C data!\n");
    return Qnil;
  }
  *data = (iodine_connection_data_s){
      .info = args,
      .subscriptions = FIO_HASH_INIT,
      .ref = 1,
      .answers_on_open = (rb_respond_to(args.handler, on_open_id) != 0),
      .answers_on_message = (rb_respond_to(args.handler, on_message_id) != 0),
      .answers_ping = (rb_respond_to(args.handler, ping_id) != 0),
      .answers_on_drained = (rb_respond_to(args.handler, on_drained_id) != 0),
      .answers_on_shutdown = (rb_respond_to(args.handler, on_shutdown_id) != 0),
      .answers_on_closed = (rb_respond_to(args.handler, on_closed_id) != 0),
      .lock = SPN_LOCK_INIT,
  };
  return connection;
}

/** Fires a connection object's event */
void iodine_connection_fire_event(VALUE connection,
                                  iodine_connection_event_type_e ev,
                                  VALUE msg) {
  if (connection == Qnil) {
    fprintf(
        stderr,
        "ERROR: (iodine) nil connection handle used by an internal API call\n");
    return;
  }
  iodine_connection_data_s *data = iodine_connection_validate_data(connection);
  if (!data) {
    fprintf(stderr,
            "ERROR: (iodine) invalid connection handle used by an "
            "internal API call: %p\n",
            (void *)connection);
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
    if (data->answers_on_closed) {
      IodineCaller.call2(data->info.handler, on_closed_id, 1, args);
    }
    spn_lock(&data->lock);
    data->info.handler = Qnil;
    data->info.env = Qnil;
    data->info.uuid = -1;
    data->info.arg = NULL;
    iodine_sub_clear_all(&data->subscriptions);
    spn_unlock(&data->lock);
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
  on_closed_id = rb_intern("on_closed");
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
    IodineStore.add(ID2SYM(on_closed_id));
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
  ConnectionKlass =
      rb_define_class_under(IodineModule, "Connection", rb_cObject);
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
