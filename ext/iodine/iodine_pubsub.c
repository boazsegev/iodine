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

VALUE IodineEngine;

static VALUE IodinePubSub;
static ID engine_varid;
static ID engine_subid;
static ID engine_pubid;
static ID engine_unsubid;
static ID default_pubsubid;

static VALUE channel_var_id;
static VALUE pattern_var_id;
static VALUE message_var_id;

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
  return Qnil;
  (void)self;
  (void)msg;
  (void)channel;
}

/* *****************************************************************************
Ruby API
***************************************************************************** */

/** @!visibility public
Called by the engine to distribute a `message` to a `channel`. Supports
`pattern` channel matching as well.

i.e.

      # Regular message distribution
      self.distribute "My Channel", "Hello!"
      # Pattern message distribution
      self.distribute "My Ch*", "Hello!", true

Returns `self`, always.

This is the ONLY method inherited from {Iodine::PubSub::Engine} that
should be called from within your code (by the engine itself).

**Notice:**

Message distribution requires both the {Iodine::PubSub::Engine} instance and the
channel to be the same.

If a client subscribed to "channel 1" on engine A, they will NOT receive
messages from "channel 1" on engine B.
*/
static VALUE engine_distribute(VALUE self, VALUE channel, VALUE msg) {
  Check_Type(channel, T_STRING);
  Check_Type(msg, T_STRING);

  iodine_engine_s *engine;
  Data_Get_Struct(self, iodine_engine_s, engine);
  FIOBJ ch = fiobj_str_new(RSTRING_PTR(channel), RSTRING_LEN(channel));
  FIOBJ m = fiobj_str_new(RSTRING_PTR(msg), RSTRING_LEN(msg));

  pubsub_publish(.engine = PUBSUB_PROCESS_ENGINE, .channel = ch, .message = m);
  fiobj_free(ch);
  fiobj_free(msg);
  return self;
}

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
  VALUE data[2];
  fio_cstr_s tmp = fiobj_obj2cstr(args->ch);
  data[0] = rb_str_new(tmp.data, tmp.len);
  data[1] = args->use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  eng = RubyCaller.call2(eng, engine_subid, 2, data);
  return NULL;
}

/* Should return 0 on success and -1 on failure. */
static void engine_subscribe(const pubsub_engine_s *eng, FIOBJ ch,
                             uint8_t use_pattern) {
  struct engine_gvl_args_s args = {
      .eng = eng, .ch = ch, .use_pattern = use_pattern,
  };
  RubyCaller.call_c(engine_subscribe_inGVL, &args) ? 0 : -1;
}

static void *engine_unsubscribe_inGVL(void *a_) {
  struct engine_gvl_args_s *args = a_;
  VALUE data[2];
  fio_cstr_s tmp = fiobj_obj2cstr(args->ch);
  data[0] = rb_str_new(tmp.data, tmp.len);
  data[1] = args->use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
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
  VALUE data[2];
  fio_cstr_s tmp = fiobj_obj2cstr(args->ch);
  data[0] = rb_str_new(tmp.data, tmp.len);
  Registry.add(data[0]);
  tmp = fiobj_obj2cstr(args->msg);
  data[1] = rb_str_new(tmp.data, tmp.len);
  Registry.add(data[1]);
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  eng = RubyCaller.call2(eng, engine_pubid, 2, data);
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

/* GMP::Integer.allocate */
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
  VALUE reply = fiobj2rb_deep(a->msg);
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
ping:: the PING interval. Default: 0 (~5 minutes).
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

/**
Subscribes the process to a channel belonging to a specific pub/sub service
(using an {Iodine::PubSub::Engine} to connect Iodine to the service).

The function accepts a single argument (a Hash) and a required block.

Accepts a single Hash argument with the following possible options:

:channel :: Required (unless :pattern). The channel to subscribe to.

:pattern :: An alternative to the required :channel, subscribes to a pattern.

*/
static VALUE iodine_subscribe(VALUE self, VALUE args) {
  Check_Type(args, T_HASH);
  rb_need_block();

  uint8_t use_pattern = 0;

  VALUE rb_ch = rb_hash_aref(args, channel_var_id);
  if (rb_ch == Qnil || rb_ch == Qfalse) {
    use_pattern = 1;
    rb_ch = rb_hash_aref(args, pattern_var_id);
    if (rb_ch == Qnil || rb_ch == Qfalse)
      rb_raise(rb_eArgError, "a channel is required for pub/sub methods.");
  }
  if (TYPE(rb_ch) == T_SYMBOL)
    rb_ch = rb_sym2str(rb_ch);
  Check_Type(rb_ch, T_STRING);

  FIOBJ ch = fiobj_str_new(RSTRING_PTR(rb_ch), RSTRING_LEN(rb_ch));
  VALUE block = rb_block_proc();

  uintptr_t subid = (uintptr_t)
      pubsub_subscribe(.channel = ch, .use_pattern = use_pattern,
                       .on_message = (block ? on_pubsub_notificationin : NULL),
                       .on_unsubscribe = (block ? iodine_on_unsubscribe : NULL),
                       .udata1 = (void *)block);
  fiobj_free(ch);
  if (!subid)
    return Qnil;
  return ULL2NUM(subid);
  (void)self;
}

/**
Cancels the subscription matching `sub_id`.
*/
static VALUE iodine_unsubscribe(VALUE self, VALUE sub_id) {
  if (sub_id == Qnil || sub_id == Qfalse)
    return Qnil;
  Check_Type(sub_id, T_FIXNUM);
  pubsub_unsubscribe((pubsub_sub_pt)NUM2LONG(sub_id));
  return Qnil;
  (void)self;
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
static VALUE iodine_publish(VALUE self, VALUE args) {
  Check_Type(args, T_HASH);
  uint8_t use_pattern = 0;

  VALUE rb_ch = rb_hash_aref(args, channel_var_id);
  if (rb_ch == Qnil || rb_ch == Qfalse) {
    use_pattern = 1;
    rb_ch = rb_hash_aref(args, pattern_var_id);
    if (rb_ch == Qnil || rb_ch == Qfalse)
      rb_raise(rb_eArgError, "channel is required for pub/sub methods.");
  }
  if (TYPE(rb_ch) == T_SYMBOL)
    rb_ch = rb_sym2str(rb_ch);
  Check_Type(rb_ch, T_STRING);

  VALUE rb_msg = rb_hash_aref(args, message_var_id);
  if (rb_msg == Qnil || rb_msg == Qfalse) {
    rb_raise(rb_eArgError, "message is required for the :publish method.");
  }
  Check_Type(rb_msg, T_STRING);

  pubsub_engine_s *engine =
      iodine_engine_ruby2facil(rb_hash_aref(args, engine_varid));
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
  engine_varid = rb_intern("engine");
  engine_subid = rb_intern("subscribe");
  engine_unsubid = rb_intern("unsubscribe");
  engine_pubid = rb_intern("publish");
  default_pubsubid = rb_intern("default_pubsub");
  channel_var_id = ID2SYM(rb_intern("channel"));
  pattern_var_id = ID2SYM(rb_intern("pattern"));
  message_var_id = ID2SYM(rb_intern("message"));

  IodinePubSub = rb_define_module_under(Iodine, "PubSub");
  IodineEngine = rb_define_class_under(IodinePubSub, "Engine", rb_cObject);

  rb_define_alloc_func(IodineEngine, engine_alloc_c);
  rb_define_method(IodineEngine, "initialize", engine_initialize, 0);

  rb_define_method(IodineEngine, "distribute", engine_distribute, 2);
  rb_define_method(IodineEngine, "subscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "unsubscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "publish", engine_pub_placeholder, 2);

  rb_define_module_function(Iodine, "default_pubsub=", ips_set_default, 1);
  rb_define_module_function(Iodine, "default_pubsub", ips_get_default, 0);

  rb_define_module_function(Iodine, "subscribe", iodine_subscribe, 1);
  rb_define_module_function(Iodine, "unsubscribe", iodine_unsubscribe, 1);
  rb_define_module_function(Iodine, "publish", iodine_publish, 1);

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
