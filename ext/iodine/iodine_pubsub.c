/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_pubsub.h"
#include "rb-call.h"

#include "pubsub.h"
#include "rb-fiobj2rb.h"
#include "redis_engine.h"
#include "websockets.h"

VALUE IodineEngine;
ID iodine_engine_pubid;

static VALUE IodinePubSub;
static VALUE IodinePubSubSubscription;
static ID engine_varid;
static ID engine_subid;
static ID engine_unsubid;
static ID default_pubsubid;

static ID to_str_shadow_id;

static VALUE as_sym_id;
static VALUE binary_sym_id;
static VALUE handler_sym_id;
static VALUE match_sym_id;
static VALUE message_sym_id;
static VALUE redis_sym_id;
static VALUE text_sym_id;
static VALUE to_sym_id;
static VALUE channel_sym_id;

/* *****************************************************************************
Mock Functions
***************************************************************************** */

/**
Override this method to handle (un)subscription requests.

This function will be called by Iodine during pub/sub (un)subscription. Don't
call this function from your own code / application.

The function should return `true` on success and `nil` or `false` on failure.
*/
static VALUE engine_sub_placeholder(VALUE self, VALUE channel,
                                    VALUE use_pattern) {
  return Qnil;
  (void)self;
  (void)channel;
  (void)use_pattern;
}

/**
Override this method to handle message publishing to the underlying engine (i.e.
from Ruby to Redis or from Ruby to MongoDB).

This function will be called by Iodine during pub/sub publication. Don't
call this function from your own code / application.

The function should return `true` on success and `nil` or `false` on failure.
*/
static VALUE engine_pub_placeholder(VALUE self, VALUE channel, VALUE msg) {
  { /* test for built-in C engines */
    iodine_engine_s *engine;
    Data_Get_Struct(self, iodine_engine_s, engine);
    if (engine->p != &engine->engine) {
      FIOBJ ch = fiobj_str_new(RSTRING_PTR(channel), RSTRING_LEN(channel));
      FIOBJ m = fiobj_str_new(RSTRING_PTR(msg), RSTRING_LEN(msg));
      pubsub_publish(.engine = engine->p, .channel = ch, .message = m);
      fiobj_free(ch);
      fiobj_free(msg);
      return Qtrue;
    }
  }
  return Qnil;
  (void)self;
  (void)msg;
  (void)channel;
}

/* *****************************************************************************
Ruby Subscription Object
***************************************************************************** */
typedef struct {
  uintptr_t subscription;
  intptr_t uuid;
  void *owner;
  iodine_pubsub_type_e type;
} iodine_subscription_s;

static inline iodine_subscription_s subscription_data(VALUE self) {
  iodine_subscription_s data = {.uuid = iodine_get_fd(self)};
  if (data.uuid && !sock_isvalid(data.uuid)) {
    iodine_set_fd(self, -1);
    data.uuid = -1;
    return data;
  }

  data.subscription =
      ((uintptr_t)NUM2LL(rb_ivar_get(self, iodine_timeout_var_id)));
  data.owner = iodine_get_cdata(self);
  if (!data.owner) {
    data.type = IODINE_PUBSUB_GLOBAL;
  } else if ((uintptr_t)data.owner & 1) {
    data.owner = (void *)((uintptr_t)data.owner & (~(uintptr_t)1));
    data.type = IODINE_PUBSUB_SSE;
  } else {
    data.type = IODINE_PUBSUB_WEBSOCKET;
  }
  return data;
}

static inline VALUE subscription_initialize(uintptr_t sub, intptr_t uuid,
                                            void *owner,
                                            iodine_pubsub_type_e type,
                                            VALUE channel) {
  VALUE self = RubyCaller.call(IodinePubSubSubscription, iodine_new_func_id);
  if (type == IODINE_PUBSUB_SSE)
    owner = (void *)((uintptr_t)owner | (uintptr_t)1);
  iodine_set_cdata(self, owner);
  iodine_set_fd(self, uuid);
  rb_ivar_set(self, to_str_shadow_id, channel);
  rb_ivar_set(self, iodine_timeout_var_id, ULL2NUM(sub));
  return self;
}

// static void set_subscription(VALUE self, pubsub_sub_pt sub) {
//   iodine_set_cdata(self, sub);
// }

/** Closes (cancels) a subscription. */
static VALUE close_subscription(VALUE self) {
  iodine_subscription_s data = subscription_data(self);
  if (!data.subscription)
    return Qnil;
  switch (data.type) {
  case IODINE_PUBSUB_GLOBAL:
    pubsub_unsubscribe((pubsub_sub_pt)data.subscription);
    break;
  case IODINE_PUBSUB_WEBSOCKET:
    websocket_unsubscribe(data.owner, data.subscription);
    break;
  case IODINE_PUBSUB_SSE:
    http_sse_unsubscribe(data.owner, data.subscription);
    break;
  }
  rb_ivar_set(self, iodine_timeout_var_id, ULL2NUM(0));
  return Qnil;
}

/** Test if the subscription's target is equal to String. */
static VALUE subscription_eq_s(VALUE self, VALUE str) {
  return rb_str_equal(rb_attr_get(self, to_str_shadow_id), str);
}

/* *****************************************************************************
Ruby API
***************************************************************************** */

pubsub_engine_s *iodine_engine_ruby2facil(VALUE ruby_engine) {
  if (ruby_engine == Qnil || ruby_engine == Qfalse)
    return NULL;
  iodine_engine_s *engine;
  Data_Get_Struct(ruby_engine, iodine_engine_s, engine);
  if (engine)
    return engine->p;
  return NULL;
}

/* *****************************************************************************
C => Ruby Bridge
***************************************************************************** */

struct engine_gvl_args_s {
  const pubsub_engine_s *eng;
  FIOBJ ch;
  FIOBJ msg;
  uint8_t use_pattern;
};

static void *engine_subscribe_inGVL(void *a_) {
  struct engine_gvl_args_s *args = a_;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  if (!eng || eng == Qnil || eng == Qfalse)
    return NULL;
  VALUE data[2];
  fio_cstr_s tmp = fiobj_obj2cstr(args->ch);
  data[1] = args->use_pattern ? Qtrue : Qnil;
  data[0] = rb_str_new(tmp.data, tmp.len);
  eng = RubyCaller.call2(eng, engine_subid, 2, data);
  return NULL;
}

/* Should return 0 on success and -1 on failure. */
static void engine_subscribe(const pubsub_engine_s *eng, FIOBJ ch,
                             uint8_t use_pattern) {
  struct engine_gvl_args_s args = {
      .eng = eng, .ch = ch, .use_pattern = use_pattern,
  };
  RubyCaller.call_c(engine_subscribe_inGVL, &args);
}

static void *engine_unsubscribe_inGVL(void *a_) {
  struct engine_gvl_args_s *args = a_;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  if (!eng || eng == Qnil || eng == Qfalse)
    return NULL;
  VALUE data[2];
  fio_cstr_s tmp = fiobj_obj2cstr(args->ch);
  data[1] = args->use_pattern ? Qtrue : Qnil;
  data[0] = rb_str_new(tmp.data, tmp.len);
  RubyCaller.call2(eng, engine_unsubid, 2, data);
  return NULL;
}

/* Return value is ignored - nothing should be returned. */
static void engine_unsubscribe(const pubsub_engine_s *eng, FIOBJ ch,
                               uint8_t use_pattern) {
  struct engine_gvl_args_s args = {
      .eng = eng, .ch = ch, .use_pattern = use_pattern,
  };
  RubyCaller.call_c(engine_unsubscribe_inGVL, &args);
}

static void *engine_publish_inGVL(void *a_) {
  struct engine_gvl_args_s *args = a_;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  if (!eng || eng == Qnil || eng == Qfalse)
    return NULL;
  VALUE data[2];
  fio_cstr_s tmp = fiobj_obj2cstr(args->ch);
  data[0] = rb_str_new(tmp.data, tmp.len);
  Registry.add(data[0]);
  tmp = fiobj_obj2cstr(args->msg);
  data[1] = rb_str_new(tmp.data, tmp.len);
  Registry.add(data[1]);
  eng = RubyCaller.call2(eng, iodine_engine_pubid, 2, data);
  Registry.remove(data[0]);
  Registry.remove(data[1]);
  return ((eng == Qfalse || eng == Qnil) ? (void *)-1 : 0);
}

/* Should return 0 on success and -1 on failure. */
static int engine_publish(const pubsub_engine_s *eng, FIOBJ ch, FIOBJ msg) {
  struct engine_gvl_args_s args = {
      .eng = eng, .ch = ch, .msg = msg,
  };
  return RubyCaller.call_c(engine_publish_inGVL, &args) ? 0 : -1;
}

/* *****************************************************************************
C <=> Ruby Data allocation
***************************************************************************** */

/* a callback for the GC (marking active objects) */
static void engine_mark(void *eng_) {
  iodine_engine_s *eng = eng_;
  rb_gc_mark(eng->handler);
}
/* a callback for the GC (marking active objects) */
static void engine_free(void *eng_) {
  iodine_engine_s *eng = eng_;
  if (eng->dealloc)
    eng->dealloc(eng->p);
  free(eng);
}

/* Iodine::PubSub::Engine.allocate */
static VALUE engine_alloc_c(VALUE self) {
  iodine_engine_s *eng = malloc(sizeof(*eng));
  if (TYPE(self) == T_CLASS)
    *eng = (iodine_engine_s){
        .handler = self,
        .engine =
            {
                .subscribe = engine_subscribe,
                .unsubscribe = engine_unsubscribe,
                .publish = engine_publish,
            },
        .p = &eng->engine,
    };

  return Data_Wrap_Struct(self, engine_mark, engine_free, eng);
}

static VALUE engine_initialize(VALUE self) {
  iodine_engine_s *engine;
  Data_Get_Struct(self, iodine_engine_s, engine);
  engine->handler = self;
  return self;
}

/* *****************************************************************************
Redis
***************************************************************************** */

struct redis_callback_data {
  FIOBJ msg;
  VALUE block;
};

/*
Perform a Redis message callback in the GVL
*/
static void *perform_redis_callback_inGVL(void *data) {
  struct redis_callback_data *a = data;
  VALUE reply = fiobj2rb_deep(a->msg, 1);
  Registry.add(reply);
  rb_funcallv(a->block, iodine_call_proc_id, 1, &reply);
  Registry.remove(a->block);
  Registry.remove(reply);
  return NULL;
}

/*
Redis message callback
*/
static void redis_callback(pubsub_engine_s *e, FIOBJ reply, void *block) {
  struct redis_callback_data d = {
      .msg = reply, .block = (VALUE)block,
  };
  RubyCaller.call_c(perform_redis_callback_inGVL, &d);
  (void)e;
}

/**
Sends commands / messages to the underlying Redis Pub connection.

The method accepts an optional callback block. i.e.:

      redis.send("Echo", "Hello World!") do |reply|
         p reply # => ["Hello World!"]
      end

This connection is only for publishing and database commands. The Sub commands,
such as SUBSCRIBE and PSUBSCRIBE, will break the engine.
*/
static VALUE redis_send(int argc, VALUE *argv, VALUE self) {
  if (argc < 1)
    rb_raise(rb_eArgError,
             "wrong number of arguments (given %d, expected at least 1).",
             argc);
  Check_Type(argv[0], T_STRING);
  FIOBJ data = FIOBJ_INVALID;
  FIOBJ cmd = FIOBJ_INVALID;
  if (argc > 1) {
    for (int i = 0; i < argc; ++i) {
      if (TYPE(argv[i]) == T_SYMBOL)
        argv[i] = rb_sym2str(argv[i]);
      if (TYPE(argv[i]) != T_FIXNUM)
        Check_Type(argv[i], T_STRING);
    }
    data = fiobj_ary_new();
    for (int i = 0; i < argc; ++i) {
      if (TYPE(argv[i]) == T_FIXNUM)
        fiobj_ary_push(data, fiobj_num_new(FIX2LONG(argv[i])));
      else
        fiobj_ary_push(
            data, fiobj_str_new(RSTRING_PTR(argv[i]), RSTRING_LEN(argv[i])));
    }
  }
  cmd = fiobj_str_new(RSTRING_PTR(argv[0]), RSTRING_LEN(argv[0]));
  iodine_engine_s *e;
  Data_Get_Struct(self, iodine_engine_s, e);

  if (rb_block_given_p()) {
    VALUE block = rb_block_proc();
    Registry.add(block);
    redis_engine_send(e->p, cmd, data, redis_callback, (void *)block);
    return block;
  } else {
    redis_engine_send(e->p, cmd, data, NULL, NULL);
  }
  fiobj_free(cmd);
  fiobj_free(data);
  return Qtrue;
}

/**
Initializes a new RedisEngine for Pub/Sub.

use:

    RedisEngine.new(address, port = 6379, ping_interval = 0)

Accepts:

address:: the Redis server's address. Required.
port:: the Redis Server port. Default: 6379
ping:: the PING interval up to 255 seconds. Default: 0 (~5 minutes).
auth:: authentication password. Default: none.
*/
static VALUE redis_engine_initialize(int argc, VALUE *argv, VALUE self) {
  if (argc < 1 || argc > 4)
    rb_raise(rb_eArgError,
             "wrong number of arguments (given %d, expected 1..4).", argc);
  VALUE address = argv[0];
  VALUE port = argc >= 2 ? argv[1] : Qnil;
  VALUE ping = argc >= 3 ? argv[2] : Qnil;
  VALUE auth = argc >= 4 ? argv[3] : Qnil;
  Check_Type(address, T_STRING);
  if (port != Qnil) {
    if (TYPE(port) == T_FIXNUM)
      port = rb_fix2str(port, 10);
    Check_Type(port, T_STRING);
  }
  if (ping != Qnil)
    Check_Type(ping, T_FIXNUM);
  if (auth != Qnil) {
    Check_Type(auth, T_STRING);
  }
  size_t iping = FIX2LONG(ping);
  if (iping > 255)
    rb_raise(rb_eRangeError, "ping_interval too big (0..255)");

  iodine_engine_s *engine;
  Data_Get_Struct(self, iodine_engine_s, engine);
  engine->handler = self;
  engine->p =
      redis_engine_create(.address = StringValueCStr(address),
                          .port =
                              (port == Qnil ? "6379" : StringValueCStr(port)),
                          .ping_interval = iping,
                          .auth = (auth == Qnil ? NULL : StringValueCStr(auth)),
                          .auth_len = (auth == Qnil ? 0 : RSTRING_LEN(auth)));
  engine->dealloc = redis_engine_destroy;
  if (!engine->p)
    rb_raise(rb_eRuntimeError, "unknown error, can't initialize RedisEngine.");
  return self;
}

/* *****************************************************************************
PubSub settings
***************************************************************************** */

/**
Sets the default Pub/Sub engine to be used.

See {Iodine::PubSub} and {Iodine::PubSub::Engine} for more details.
*/
static VALUE ips_set_default(VALUE self, VALUE en) {
  iodine_engine_s *e;
  Data_Get_Struct(en, iodine_engine_s, e);
  if (!e)
    rb_raise(rb_eArgError, "deafult engine must be an Iodine::PubSub::Engine.");
  if (!e->p)
    rb_raise(rb_eArgError, "This Iodine::PubSub::Engine is broken.");
  rb_ivar_set(self, default_pubsubid, en);
  PUBSUB_DEFAULT_ENGINE = e->p;
  return en;
}

/**
Returns the default Pub/Sub engine (if any).

See {Iodine::PubSub} and {Iodine::PubSub::Engine} for more details.
*/
static VALUE ips_get_default(VALUE self) {
  return rb_ivar_get(self, default_pubsubid);
}

/* *****************************************************************************
Pub/Sub API
***************************************************************************** */

static void iodine_on_unsubscribe(void *u1, void *u2) {
  if (u1 && (VALUE)u1 != Qnil && u1 != (VALUE)Qfalse)
    Registry.remove((VALUE)u1);
  (void)u2;
}

static void *on_pubsub_notificationinGVL(pubsub_message_s *n) {
  VALUE rbn[2];
  fio_cstr_s tmp = fiobj_obj2cstr(n->channel);
  rbn[0] = rb_str_new(tmp.data, tmp.len);
  Registry.add(rbn[0]);
  tmp = fiobj_obj2cstr(n->message);
  rbn[1] = rb_str_new(tmp.data, tmp.len);
  Registry.add(rbn[1]);
  RubyCaller.call2((VALUE)n->udata1, iodine_call_proc_id, 2, rbn);
  Registry.remove(rbn[0]);
  Registry.remove(rbn[1]);
  return NULL;
}

static void on_pubsub_notificationin(pubsub_message_s *n) {
  RubyCaller.call_c((void *(*)(void *))on_pubsub_notificationinGVL, n);
}

static void iodine_on_unsubscribe_ws(void *u) {
  if (u && (VALUE)u != Qnil && u != (VALUE)Qfalse)
    Registry.remove((VALUE)u);
}

static void *
on_pubsub_notificationinGVL_ws(websocket_pubsub_notification_s *n) {
  VALUE rbn[2];
  fio_cstr_s tmp = fiobj_obj2cstr(n->channel);
  rbn[0] = rb_str_new(tmp.data, tmp.len);
  Registry.add(rbn[0]);
  tmp = fiobj_obj2cstr(n->message);
  rbn[1] = rb_str_new(tmp.data, tmp.len);
  Registry.add(rbn[1]);
  RubyCaller.call2((VALUE)n->udata, iodine_call_proc_id, 2, rbn);
  Registry.remove(rbn[0]);
  Registry.remove(rbn[1]);
  return NULL;
}

static void on_pubsub_notificationin_ws(websocket_pubsub_notification_s n) {
  RubyCaller.call_c((void *(*)(void *))on_pubsub_notificationinGVL_ws, &n);
}

static void on_pubsub_notificationin_sse(http_sse_s *sse, FIOBJ channel,
                                         FIOBJ message, void *udata) {
  websocket_pubsub_notification_s n = {
      .channel = channel, .message = message, .udata = udata};
  RubyCaller.call_c((void *(*)(void *))on_pubsub_notificationinGVL, &n);
  (void)sse;
}

/** Subscribes to a Pub/Sub channel - internal implementation */
VALUE iodine_subscribe(int argc, VALUE *argv, void *owner,
                       iodine_pubsub_type_e type) {

  VALUE rb_ch = Qnil;
  VALUE rb_opt = 0;
  VALUE block = 0;
  uint8_t use_pattern = 0, force_text = 1, force_binary = 0;
  intptr_t uuid = 0;

  switch (argc) {
  case 2:
    rb_ch = argv[0];
    rb_opt = argv[1];
    break;
  case 1:
    /* single argument must be a Hash / channel name */
    if (TYPE(argv[0]) == T_HASH) {
      rb_opt = argv[0];
      rb_ch = rb_hash_aref(argv[0], to_sym_id);
      if (rb_ch == Qnil || rb_ch == Qfalse) {
        /* temporary backport support */
        rb_ch = rb_hash_aref(argv[0], channel_sym_id);
        if (rb_ch) {
          fprintf(stderr,
                  "WARNING: use of :channel in subscribe is deprecated.\n");
        }
      }
    } else {
      rb_ch = argv[0];
    }
    break;
  default:
    rb_raise(rb_eArgError, "method accepts 1 or 2 arguments.");
    return Qnil;
  }

  if (rb_ch == Qnil || rb_ch == Qfalse) {
    rb_raise(rb_eArgError,
             "a target (:to) subject / stream / channel is required.");
  }

  if (TYPE(rb_ch) == T_SYMBOL)
    rb_ch = rb_sym2str(rb_ch);
  Check_Type(rb_ch, T_STRING);

  if (rb_opt) {
    if (type == IODINE_PUBSUB_WEBSOCKET &&
        rb_hash_aref(rb_opt, as_sym_id) == binary_sym_id) {
      force_text = 0;
      force_binary = 1;
    }
    if (rb_hash_aref(rb_opt, match_sym_id) == redis_sym_id) {
      use_pattern = 1;
    }
    block = rb_hash_aref(rb_opt, handler_sym_id);
    if (block != Qnil)
      Registry.add(block);
  }

  if (block == Qnil) {
    if (rb_block_given_p()) {
      block = rb_block_proc();
      Registry.add(block);
    } else if (type == IODINE_PUBSUB_GLOBAL) {
      rb_need_block();
      return Qnil;
    }
  }
  if (block == Qnil)
    block = 0;

  FIOBJ ch = fiobj_str_new(RSTRING_PTR(rb_ch), RSTRING_LEN(rb_ch));

  uintptr_t sub = 0;
  switch (type) {
  case IODINE_PUBSUB_GLOBAL:
    sub = (uintptr_t)pubsub_subscribe(.channel = ch, .use_pattern = use_pattern,
                                      .on_message = on_pubsub_notificationin,
                                      .on_unsubscribe = iodine_on_unsubscribe,
                                      .udata1 = (void *)block);
    break;
  case IODINE_PUBSUB_WEBSOCKET:
    uuid = websocket_uuid(owner);
    sub = websocket_subscribe(
        owner, .channel = ch, .use_pattern = use_pattern,
        .force_text = force_text, .force_binary = force_binary,
        .on_message = (block ? on_pubsub_notificationin_ws : NULL),
        .on_unsubscribe = (block ? iodine_on_unsubscribe_ws : NULL),
        .udata = (void *)block);
    break;
  case IODINE_PUBSUB_SSE:
    uuid = http_sse2uuid(owner);
    sub = http_sse_subscribe(
        owner, .channel = ch, .use_pattern = use_pattern,
        .on_message = (block ? on_pubsub_notificationin_sse : NULL),
        .on_unsubscribe = (block ? iodine_on_unsubscribe_ws : NULL),
        .udata = (void *)block);

    break;
  }

  fiobj_free(ch);
  if (!sub)
    return Qnil;
  return subscription_initialize(sub, uuid, owner, type, rb_ch);
}

// clang-format off
/**
Subscribes to a Pub/Sub channel.

The method accepts 1-2 arguments and an optional block. These are all valid ways
to call the method:

      subscribe("my_stream") {|from, msg| p msg }
      subscribe("my_stream", match: :redis) {|from, msg| p msg }
      subscribe(to: "my_stream")  {|from, msg| p msg }
      subscribe to: "my_stream", match: :redis, handler: MyProc

The first argument must be either a String or a Hash.

The second, optional, argument must be a Hash (if given).

The options Hash supports the following possible keys (other keys are ignored, all keys are Symbols):

:match :: The channel / subject name matching type to be used. Valid value is: `:redis`. Future versions hope to support `:nats` and `:rabbit` patern matching as well.

:to :: The channel / subject to subscribe to.

Returns an {Iodine::PubSub::Subscription} object that answers to:

close :: closes the connection.
to_s :: returns the subscription's target (stream / channel / subject).
==(str) :: returns true if the string is an exact match for the target (even if the target itself is a pattern).

*/
static VALUE iodine_subscribe_global(int argc, VALUE *argv, VALUE self) {
  // clang-format on
  return iodine_subscribe(argc, argv, NULL, IODINE_PUBSUB_GLOBAL);
  (void)self;
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
VALUE iodine_publish(int argc, VALUE *argv, VALUE self) {
  VALUE rb_ch, rb_msg, rb_engine = Qnil;
  uint8_t use_pattern = 0;
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
    rb_ch = rb_hash_aref(argv[0], to_sym_id);
    if (rb_ch == Qnil || rb_ch == Qfalse) {
      use_pattern = 1;
      rb_ch = rb_hash_aref(argv[0], match_sym_id);
    }
    rb_msg = rb_hash_aref(argv[0], message_sym_id);
    rb_engine = rb_hash_aref(argv[0], engine_varid);
  } break;
  default:
    rb_raise(rb_eArgError, "method accepts 1-3 arguments.");
  }

  if (rb_msg == Qnil || rb_msg == Qfalse) {
    rb_raise(rb_eArgError, "message is required.");
  }
  Check_Type(rb_msg, T_STRING);

  if (rb_ch == Qnil || rb_ch == Qfalse)
    rb_raise(rb_eArgError, "channel is required .");
  if (TYPE(rb_ch) == T_SYMBOL)
    rb_ch = rb_sym2str(rb_ch);
  Check_Type(rb_ch, T_STRING);

  if (rb_engine == Qfalse) {
    engine = PUBSUB_PROCESS_ENGINE;
  } else if (rb_engine == Qnil) {
    engine = NULL;
  } else {
    engine = iodine_engine_ruby2facil(rb_engine);
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
Initialization
***************************************************************************** */
void Iodine_init_pubsub(void) {
  default_pubsubid = rb_intern("default_pubsub");
  engine_subid = rb_intern("subscribe");
  engine_unsubid = rb_intern("unsubscribe");
  engine_varid = rb_intern("engine");
  iodine_engine_pubid = rb_intern("publish");
  to_str_shadow_id = rb_intern("@to_s");

  as_sym_id = ID2SYM(rb_intern("as"));
  binary_sym_id = ID2SYM(rb_intern("binary"));
  handler_sym_id = ID2SYM(rb_intern("handler"));
  match_sym_id = ID2SYM(rb_intern("match"));
  message_sym_id = ID2SYM(rb_intern("message"));
  redis_sym_id = ID2SYM(rb_intern("redis"));
  text_sym_id = ID2SYM(rb_intern("text"));
  to_sym_id = ID2SYM(rb_intern("to"));

  channel_sym_id = ID2SYM(rb_intern("channel")); /* bawards compatibility */

  IodinePubSub = rb_define_module_under(Iodine, "PubSub");
  IodineEngine = rb_define_class_under(IodinePubSub, "Engine", rb_cObject);
  IodinePubSubSubscription =
      rb_define_class_under(IodinePubSub, "Subscription", rb_cObject);

  rb_define_method(IodinePubSubSubscription, "close", close_subscription, 0);
  rb_define_method(IodinePubSubSubscription, "==", subscription_eq_s, 1);
  rb_attr(IodinePubSubSubscription, rb_intern("to_s"), 1, 0, 1);

  rb_define_alloc_func(IodineEngine, engine_alloc_c);
  rb_define_method(IodineEngine, "initialize", engine_initialize, 0);

  rb_define_method(IodineEngine, "subscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "unsubscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "publish", engine_pub_placeholder, 2);

  rb_define_module_function(Iodine, "default_pubsub=", ips_set_default, 1);
  rb_define_module_function(Iodine, "default_pubsub", ips_get_default, 0);

  rb_define_module_function(Iodine, "subscribe", iodine_subscribe_global, -1);
  rb_define_module_function(Iodine, "publish", iodine_publish, -1);

  /* *************************
  Initialize C pubsub engines
  ************************** */
  VALUE engine_in_c;
  iodine_engine_s *engine;

  engine_in_c = rb_funcallv(IodineEngine, iodine_new_func_id, 0, NULL);
  Data_Get_Struct(engine_in_c, iodine_engine_s, engine);
  engine->p = (pubsub_engine_s *)PUBSUB_CLUSTER_ENGINE;

  /** This is the (currently) default pub/sub engine. It will distribute
   * messages to all subscribers in the process cluster. */
  rb_define_const(IodinePubSub, "CLUSTER", engine_in_c);

  // rb_const_set(IodineEngine, rb_intern("CLUSTER"), e);

  engine_in_c = rb_funcallv(IodineEngine, iodine_new_func_id, 0, NULL);
  Data_Get_Struct(engine_in_c, iodine_engine_s, engine);
  engine->p = (pubsub_engine_s *)PUBSUB_PROCESS_ENGINE;

  /** This is a single process pub/sub engine. It will distribute messages to
   * all subscribers sharing the same process. */
  rb_define_const(IodinePubSub, "SINGLE_PROCESS", engine_in_c);

  // rb_const_set(IodineEngine, rb_intern("SINGLE_PROCESS"), e);

  engine_in_c =
      rb_define_class_under(IodinePubSub, "RedisEngine", IodineEngine);
  rb_define_method(engine_in_c, "initialize", redis_engine_initialize, -1);
  rb_define_method(engine_in_c, "send", redis_send, -1);
}
