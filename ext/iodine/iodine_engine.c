/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_engine.h"
#include "rb-call.h"

#include "pubsub.h"
#include "redis_engine.h"

VALUE IodineEngine;

static VALUE IodinePubSub;
static ID engine_varid;
static ID engine_subid;
static ID engine_pubid;
static ID engine_unsubid;

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
static VALUE engine_pub_placeholder(VALUE self, VALUE channel, VALUE msg,
                                    VALUE use_pattern) {
  return Qnil;
  (void)self;
  (void)msg;
  (void)channel;
  (void)use_pattern;
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
static VALUE engine_distribute(int argc, VALUE *argv, VALUE self) {
  if (argc < 2 || argc > 3)
    rb_raise(rb_eArgError,
             "wrong number of arguments (given %d, expected 2..3).", argc);
  VALUE channel = argv[0];
  VALUE msg = argv[1];
  VALUE pattern = argc >= 3 ? argv[2] : Qnil;
  Check_Type(channel, T_STRING);
  Check_Type(msg, T_STRING);

  iodine_engine_s *engine;
  Data_Get_Struct(self, iodine_engine_s, engine);

  pubsub_engine_distribute(.engine = engine->p,
                           .channel.name = RSTRING_PTR(channel),
                           .channel.len = RSTRING_LEN(channel),
                           .msg.data = RSTRING_PTR(msg),
                           .msg.len = RSTRING_LEN(msg),
                           .use_pattern =
                               (pattern != Qnil && pattern != Qfalse));
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
  const char *ch;
  size_t ch_len;
  const char *msg;
  size_t msg_len;
  uint8_t use_pattern;
};

static void *engine_subscribe_inGVL(void *a_) {
  struct engine_gvl_args_s *args = a_;
  VALUE data[2];
  data[0] = rb_str_new(args->ch, args->ch_len);
  data[1] = args->use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  eng = RubyCaller.call2(eng, engine_subid, 2, data);
  return ((eng == Qfalse || eng == Qnil) ? (void *)-1 : (void *)0);
}

/* Should return 0 on success and -1 on failure. */
static int engine_subscribe(const pubsub_engine_s *eng, const char *ch,
                            size_t ch_len, uint8_t use_pattern) {
  struct engine_gvl_args_s args = {
      .eng = eng, .ch = ch, .ch_len = ch_len, .use_pattern = use_pattern,
  };
  return RubyCaller.call_c(engine_subscribe_inGVL, &args) ? 0 : -1;
}

static void *engine_unsubscribe_inGVL(void *a_) {
  struct engine_gvl_args_s *args = a_;
  VALUE data[2];
  data[0] = rb_str_new(args->ch, args->ch_len);
  data[1] = args->use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  RubyCaller.call2(eng, engine_unsubid, 2, data);
  return NULL;
}

/* Return value is ignored - nothing should be returned. */
static void engine_unsubscribe(const pubsub_engine_s *eng, const char *ch,
                               size_t ch_len, uint8_t use_pattern) {
  struct engine_gvl_args_s args = {
      .eng = eng, .ch = ch, .ch_len = ch_len, .use_pattern = use_pattern,
  };
  RubyCaller.call_c(engine_unsubscribe_inGVL, &args);
}

static void *engine_publish_inGVL(void *a_) {
  struct engine_gvl_args_s *args = a_;
  VALUE data[3];
  data[0] = rb_str_new(args->ch, args->ch_len);
  data[1] = rb_str_new(args->msg, args->msg_len);
  data[2] = args->use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)args->eng)->handler;
  eng = RubyCaller.call2(eng, engine_pubid, 3, data);
  return ((eng == Qfalse || eng == Qnil) ? (void *)-1 : 0);
}

/* Should return 0 on success and -1 on failure. */
static int engine_publish(const pubsub_engine_s *eng, const char *ch,
                          size_t ch_len, const char *msg, size_t msg_len,
                          uint8_t use_pattern) {
  struct engine_gvl_args_s args = {
      .eng = eng,
      .ch = ch,
      .ch_len = ch_len,
      .msg = msg,
      .msg_len = msg_len,
      .use_pattern = use_pattern,
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
  resp_object_s *msg;
  VALUE block;
};

/*
populate
*/
int populate_redis_callback_reply(resp_parser_pt p, resp_object_s *o,
                                  void *rep) {
  switch (o->type) {
  case RESP_ARRAY:
  case RESP_PUBSUB:
    break;
  case RESP_NULL:
    rb_ary_push((VALUE)rep, Qnil);
  case RESP_NUMBER:
    rb_ary_push((VALUE)rep, LONG2NUM(resp_obj2num(o)->number));
    break;
  case RESP_ERR:
  case RESP_STRING:
    rb_ary_push((VALUE)rep, rb_str_new((char *)resp_obj2str(o)->string,
                                       resp_obj2str(o)->len));
    break;
  case RESP_OK:
    rb_ary_push((VALUE)rep, rb_str_new("OK", 2));
    break;
  }
  return 0;
  (void)p;
}
/*
Perform a Redis message callback in the GVL
*/
static void *perform_redis_callback_inGVL(void *data) {
  struct redis_callback_data *a = data;
  VALUE reply = rb_ary_new();
  resp_obj_each(NULL, a->msg, populate_redis_callback_reply, (void *)reply);
  rb_funcall(a->block, iodine_call_proc_id, 1, &reply);
  Registry.remove(a->block);
  return NULL;
}

/*
Redis message callback
*/
static void redis_callback(pubsub_engine_s *e, resp_object_s *msg,
                           void *block) {
  struct redis_callback_data d = {
      .msg = msg, .block = (VALUE)block,
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
  resp_object_s *cmd = NULL;
  Check_Type(argv[0], T_STRING);

  iodine_engine_s *e;
  Data_Get_Struct(self, iodine_engine_s, e);
  cmd = resp_arr2obj(argc, NULL);
  for (int i = 0; i < argc; i++) {
    switch (TYPE(argv[i])) {
    case T_SYMBOL:
      argv[i] = rb_sym2str(argv[i]);
    /* Fallthrough */
    case T_STRING:
      resp_obj2arr(cmd)->array[i] =
          resp_str2obj(RSTRING_PTR(argv[i]), RSTRING_LEN(argv[i]));
      break;
    case T_FIXNUM:
      resp_obj2arr(cmd)->array[i] = resp_num2obj(FIX2LONG(argv[i]));
      break;
    default:
      goto error;
      break;
    }
  }

  if (rb_block_given_p()) {
    VALUE block = Qnil;
    block = rb_block_proc();
    Registry.add(block);
    redis_engine_send(e->p, cmd, redis_callback, (void *)block);
    return block;
  } else {
    redis_engine_send(e->p, cmd, NULL, NULL);
  }
  return Qnil;
error:
  if (cmd)
    resp_free_object(cmd);
  rb_raise(rb_eArgError, "Arguments can only include Strings, Symbols and "
                         "Integers - no arrays or hashes or other objects can "
                         "be sent.");
  return self;
}

/**
Initializes a new RedisEngine for Pub/Sub.

use:

    RedisEngine.new(address, port = 6379, ping_interval = 0)

Accepts:

@address:: the Redis server's address. Required.
@port:: the Redis Server port. Default: 6379
@ping:: the PING interval. Default: 0 (~5 minutes).
@auth:: authentication password. Default: none.
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
  if (!engine->p)
    rb_raise(rb_eRuntimeError, "unknown error, can't initialize RedisEngine.");
  engine->dealloc = redis_engine_destroy;
  return self;
}

/* *****************************************************************************
Initialization
***************************************************************************** */
void Iodine_init_engine(void) {
  engine_varid = rb_intern("engine");
  engine_subid = rb_intern("subscribe");
  engine_unsubid = rb_intern("unsubscribe");
  engine_pubid = rb_intern("publish");

  IodinePubSub = rb_define_module_under(Iodine, "PubSub");
  IodineEngine = rb_define_class_under(IodinePubSub, "Engine", rb_cObject);

  rb_define_alloc_func(IodineEngine, engine_alloc_c);
  rb_define_method(IodineEngine, "initialize", engine_initialize, 0);

  rb_define_method(IodineEngine, "distribute", engine_distribute, -1);
  rb_define_method(IodineEngine, "subscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "unsubscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "publish", engine_pub_placeholder, 3);

  /* *************************
  Initialize C pubsub engines
  ************************** */
  VALUE engine_in_c;
  iodine_engine_s *engine;

  engine_in_c = rb_funcallv(IodineEngine, iodine_new_func_id, 0, NULL);
  Data_Get_Struct(engine_in_c, iodine_engine_s, engine);
  engine->p = (pubsub_engine_s *)&PUBSUB_CLUSTER_ENGINE;

  /** This is the (currently) default pub/sub engine. It will distribute
   * messages to all subscribers in the process cluster. */
  rb_define_const(IodinePubSub, "CLUSTER", engine_in_c);
  // rb_const_set(IodineEngine, rb_intern("CLUSTER"), e);

  engine_in_c = rb_funcallv(IodineEngine, iodine_new_func_id, 0, NULL);
  Data_Get_Struct(engine_in_c, iodine_engine_s, engine);
  engine->p = (pubsub_engine_s *)&PUBSUB_CLUSTER_ENGINE;

  /** This is a single process pub/sub engine. It will distribute messages to
   * all subscribers sharing the same process. */
  rb_define_const(IodinePubSub, "SINGLE_PROCESS", engine_in_c);
  // rb_const_set(IodineEngine, rb_intern("SINGLE_PROCESS"), e);

  engine_in_c =
      rb_define_class_under(IodinePubSub, "RedisEngine", IodineEngine);
  rb_define_method(engine_in_c, "initialize", redis_engine_initialize, -1);
  rb_define_method(engine_in_c, "send", redis_send, -1);
}
