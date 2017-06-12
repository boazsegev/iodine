/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_engine.h"
#include "rb-call.h"

#include "pubsub.h"

VALUE IodineEngine;

static VALUE IodinePubSub;
static ID cluster_varid;
static ID engine_varid;
static ID engine_subid;
static ID engine_pubid;
static ID engine_unsubid;

typedef struct {
  pubsub_engine_s engine;
  VALUE handler;
} iodine_engine_s;

/* *****************************************************************************
Mock Functions
***************************************************************************** */

/**
Override this method to handle (un)subscription requests.

This function will be called by Iodine during pub/sub (un)subscription. Don't
call this function from your own code / application.

The function should return `true` on success and `nil` or `false` on failure.
*/
VALUE engine_sub_placeholder(VALUE self, VALUE channel, VALUE use_pattern) {
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
VALUE engine_pub_placeholder(VALUE self, VALUE channel, VALUE msg,
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

  VALUE push2cluster = rb_ivar_get(self, cluster_varid);

  pubsub_engine_s *engine;
  Data_Get_Struct(self, pubsub_engine_s, engine);

  engine->push2cluster = (push2cluster != Qnil && push2cluster != Qfalse),

  pubsub_engine_distribute(.engine = engine,
                           .channel.name = RSTRING_PTR(channel),
                           .channel.len = RSTRING_LEN(channel),
                           .msg.data = RSTRING_PTR(msg),
                           .msg.len = RSTRING_LEN(msg),
                           .use_pattern =
                               (pattern != Qnil && pattern != Qfalse));
  return self;
}

/* *****************************************************************************
C => Ruby Bridge
***************************************************************************** */

/* Should return 0 on success and -1 on failure. */
static int engine_subscribe(const pubsub_engine_s *eng_, const char *ch,
                            size_t ch_len, uint8_t use_pattern) {
  VALUE data[2];
  data[0] = rb_str_new(ch, ch_len);
  data[1] = use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)eng_)->handler;
  eng = RubyCaller.call2(eng, engine_subid, 2, data);
  return ((eng == Qfalse || eng == Qnil) ? -1 : 0);
}

/* Return value is ignored - nothing should be returned. */
static void engine_unsubscribe(const pubsub_engine_s *eng_, const char *ch,
                               size_t ch_len, uint8_t use_pattern) {
  VALUE data[2];
  data[0] = rb_str_new(ch, ch_len);
  data[1] = use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)eng_)->handler;
  RubyCaller.call2(eng, engine_subid, 2, data);
}

/* Should return 0 on success and -1 on failure. */
static int engine_publish(const pubsub_engine_s *eng_, const char *ch,
                          size_t ch_len, const char *msg, size_t msg_len,
                          uint8_t use_pattern) {
  VALUE data[3];
  data[0] = rb_str_new(ch, ch_len);
  data[1] = rb_str_new(msg, msg_len);
  data[2] = use_pattern ? Qtrue : Qnil;
  VALUE eng = ((iodine_engine_s *)eng_)->handler;
  eng = RubyCaller.call2(eng, engine_subid, 3, data);
  return ((eng == Qfalse || eng == Qnil) ? -1 : 0);
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
  /* what to do ?*/
  free(eng);
}

/* GMP::Integer.allocate */
VALUE engine_alloc_c(VALUE self) {
  iodine_engine_s *eng = malloc(sizeof(*eng));

  *eng = (iodine_engine_s){
      .handler = self,
      .engine.subscribe = engine_subscribe,
      .engine.unsubscribe = engine_unsubscribe,
      .engine.publish = engine_publish,
  };
  // Registry.add(self);
  return Data_Wrap_Struct(self, engine_free, NULL, eng);
}
/* *****************************************************************************
Initialization
***************************************************************************** */
void Iodine_init_engine(void) {
  struct rb_data_type_struct s;
  cluster_varid = rb_intern("@push2cluster");
  engine_varid = rb_intern("engine");
  engine_subid = rb_intern("subscribe");
  engine_pubid = rb_intern("unsubscribe");
  engine_unsubid = rb_intern("publish");

  IodinePubSub = rb_define_module_under(Iodine, "PubSub");
  IodineEngine = rb_define_class_under(IodinePubSub, "Engine", Qnil);
  rb_define_protected_method(IodineEngine, "distribute", engine_distribute, -1);
  rb_define_method(IodineEngine, "subscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "unsubscribe", engine_sub_placeholder, 2);
  rb_define_method(IodineEngine, "publish", engine_pub_placeholder, 3);
  rb_define_alloc_func(IodineEngine, engine_alloc_c);
}
