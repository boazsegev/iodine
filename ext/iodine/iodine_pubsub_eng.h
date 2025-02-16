#ifndef H___IODINE_PUBSUB_ENG___H
#define H___IODINE_PUBSUB_ENG___H
#include "iodine.h"

/* *****************************************************************************
Ruby PubSub Engine Type
***************************************************************************** */

typedef struct iodine_pubsub_eng_s {
  fio_pubsub_engine_s engine;
  fio_pubsub_engine_s *ptr;
  VALUE handler;
} iodine_pubsub_eng_s;

/* *****************************************************************************
Ruby PubSub Engine Bridge
***************************************************************************** */

typedef struct iodine_pubsub_eng___args_s {
  iodine_pubsub_eng_s *eng;
  fio_msg_s *msg;
  fio_buf_info_s channel;
  int16_t filter;
} iodine_pubsub_eng___args_s;

/** Called after the engine was detached, may be used for cleanup. */
static void iodine_pubsub_eng___detached(const fio_pubsub_engine_s *eng) {
  iodine_pubsub_eng_s *e = (iodine_pubsub_eng_s *)eng;
  iodine_ruby_call_outside(e->handler, rb_intern("on_cleanup"));
}

static void *iodine_pubsub_eng___subscribe__in_GC(void *a_) {
  iodine_pubsub_eng___args_s *args = (iodine_pubsub_eng___args_s *)a_;
  VALUE ch = rb_str_new(args->channel.buf, args->channel.len);
  STORE.hold(ch);
  iodine_ruby_call_inside(args->eng->handler, rb_intern("subscribe"), 1, &ch);
  STORE.release(ch);
  return NULL;
}
/** Subscribes to a channel. Called ONLY in the Root (master) process. */
static void iodine_pubsub_eng___subscribe(const fio_pubsub_engine_s *eng,
                                          fio_buf_info_s channel,
                                          int16_t filter) {
  iodine_pubsub_eng___args_s args = {
      .eng = (iodine_pubsub_eng_s *)eng,
      .channel = channel,
      .filter = filter,
  };
  rb_thread_call_with_gvl(iodine_pubsub_eng___subscribe__in_GC, &args);
}

static void *iodine_pubsub_eng___psubscribe__in_GC(void *a_) {
  iodine_pubsub_eng___args_s *args = (iodine_pubsub_eng___args_s *)a_;
  VALUE ch = rb_str_new(args->channel.buf, args->channel.len);
  STORE.hold(ch);
  iodine_ruby_call_inside(args->eng->handler, rb_intern("psubscribe"), 1, &ch);
  STORE.release(ch);
  return NULL;
}
/** Subscribes to a pattern. Called ONLY in the Root (master) process. */
static void iodine_pubsub_eng___psubscribe(const fio_pubsub_engine_s *eng,
                                           fio_buf_info_s channel,
                                           int16_t filter) {
  iodine_pubsub_eng___args_s args = {
      .eng = (iodine_pubsub_eng_s *)eng,
      .channel = channel,
      .filter = filter,
  };
  rb_thread_call_with_gvl(iodine_pubsub_eng___psubscribe__in_GC, &args);
}

static void *iodine_pubsub_eng___unsubscribe__in_GC(void *a_) {
  iodine_pubsub_eng___args_s *args = (iodine_pubsub_eng___args_s *)a_;
  VALUE ch = rb_str_new(args->channel.buf, args->channel.len);
  STORE.hold(ch);
  iodine_ruby_call_inside(args->eng->handler, rb_intern("unsubscribe"), 1, &ch);
  STORE.release(ch);
  return NULL;
}
/** Unsubscribes to a channel. Called ONLY in the Root (master) process. */
static void iodine_pubsub_eng___unsubscribe(const fio_pubsub_engine_s *eng,
                                            fio_buf_info_s channel,
                                            int16_t filter) {
  iodine_pubsub_eng___args_s args = {
      .eng = (iodine_pubsub_eng_s *)eng,
      .channel = channel,
      .filter = filter,
  };
  rb_thread_call_with_gvl(iodine_pubsub_eng___unsubscribe__in_GC, &args);
}

static void *iodine_pubsub_eng___punsubscribe__in_GC(void *a_) {
  iodine_pubsub_eng___args_s *args = (iodine_pubsub_eng___args_s *)a_;
  VALUE ch = rb_str_new(args->channel.buf, args->channel.len);
  STORE.hold(ch);
  iodine_ruby_call_inside(args->eng->handler,
                          rb_intern("punsubscribe"),
                          1,
                          &ch);
  STORE.release(ch);
  return NULL;
}
/** Unsubscribe to a pattern. Called ONLY in the Root (master) process. */
static void iodine_pubsub_eng___punsubscribe(const fio_pubsub_engine_s *eng,
                                             fio_buf_info_s channel,
                                             int16_t filter) {
  iodine_pubsub_eng___args_s args = {
      .eng = (iodine_pubsub_eng_s *)eng,
      .channel = channel,
      .filter = filter,
  };
  rb_thread_call_with_gvl(iodine_pubsub_eng___punsubscribe__in_GC, &args);
}

static void *iodine_pubsub_eng___publish__in_GC(void *a_) {
  iodine_pubsub_eng___args_s *args = (iodine_pubsub_eng___args_s *)a_;
  VALUE msg = iodine_pubsub_msg_new(args->msg);
  iodine_ruby_call_inside(args->eng->handler, rb_intern("publish"), 1, &msg);
  STORE.release(msg);
  return NULL;
}

/** Publishes a message through the engine. Called by any worker / thread. */
static void iodine_pubsub_eng___publish(const fio_pubsub_engine_s *eng,
                                        fio_msg_s *msg) {
  iodine_pubsub_eng___args_s args = {
      .eng = (iodine_pubsub_eng_s *)eng,
      .msg = msg,
  };
  rb_thread_call_with_gvl(iodine_pubsub_eng___publish__in_GC, &args);
}

static fio_pubsub_engine_s iodine_pubsub___engine_validate(VALUE obj) {
  fio_pubsub_engine_s r = {
      /** Called after the engine was detached, may be used for cleanup. */
      .detached = (rb_respond_to(obj, rb_intern("on_cleanup")))
                      ? iodine_pubsub_eng___detached
                      : NULL,
      /** Subscribes to a channel. Called ONLY in the Root (master) process. */
      .subscribe = (rb_respond_to(obj, rb_intern("subscribe")))
                       ? iodine_pubsub_eng___subscribe
                       : NULL,
      /** Subscribes to a pattern. Called ONLY in the Root (master) process. */
      .psubscribe = (rb_respond_to(obj, rb_intern("psubscribe")))
                        ? iodine_pubsub_eng___psubscribe
                        : NULL,
      /** Unsubscribes to a channel. Called ONLY in the Root (master) process.
       */
      .unsubscribe = (rb_respond_to(obj, rb_intern("unsubscribe")))
                         ? iodine_pubsub_eng___unsubscribe
                         : NULL,
      /** Unsubscribe to a pattern. Called ONLY in the Root (master) process. */
      .punsubscribe = (rb_respond_to(obj, rb_intern("punsubscribe")))
                          ? iodine_pubsub_eng___punsubscribe
                          : NULL,
      /** Publishes a message through the engine. Called by any worker / thread.
       */
      .publish = (rb_respond_to(obj, rb_intern("publish")))
                     ? iodine_pubsub_eng___publish
                     : NULL,
  };
  return r;
}
/* *****************************************************************************
Ruby PubSub Engine Object
***************************************************************************** */

static size_t iodine_pubsub_eng_data_size(const void *ptr_) {
  iodine_pubsub_eng_s *m = (iodine_pubsub_eng_s *)ptr_;
  return sizeof(*m);
}

static void iodine_pubsub_eng_free(void *ptr_) {
  iodine_pubsub_eng_s *e = (iodine_pubsub_eng_s *)ptr_;
  if (FIO_PUBSUB_DEFAULT == e->ptr)
    FIO_PUBSUB_DEFAULT = NULL;
  ruby_xfree(e);
  // FIO_LEAK_COUNTER_ON_FREE(iodine_pubsub_eng);
}

static const rb_data_type_t IODINE_PUBSUB_ENG_DATA_TYPE = {
    .wrap_struct_name = "IodinePSEngine",
    .function =
        {
            .dfree = iodine_pubsub_eng_free,
            .dsize = iodine_pubsub_eng_data_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE iodine_pubsub_eng_alloc(VALUE klass) {
  /* use Ruby allocator for long life object */
  iodine_pubsub_eng_s *m = (iodine_pubsub_eng_s *)ruby_xmalloc(sizeof(*m));
  if (!m)
    goto no_memory;
  *m = (iodine_pubsub_eng_s){0};
  m->ptr = &m->engine;
  // FIO_LEAK_COUNTER_ON_ALLOC(iodine_pubsub_eng);
  m->handler = TypedData_Wrap_Struct(klass, &IODINE_PUBSUB_ENG_DATA_TYPE, m);
  m->engine = iodine_pubsub___engine_validate(m->handler);
  return m->handler;

no_memory:
  FIO_LOG_FATAL("Memory allocation failed");
  fio_io_stop();
  return Qnil;
}

static iodine_pubsub_eng_s *iodine_pubsub_eng_get(VALUE self) {
  iodine_pubsub_eng_s *m;
  TypedData_Get_Struct(self,
                       iodine_pubsub_eng_s,
                       &IODINE_PUBSUB_ENG_DATA_TYPE,
                       m);
  return m;
}

/* *****************************************************************************
Ruby Methods
***************************************************************************** */

static VALUE iodine_pubsub_eng_initialize(VALUE self) {
  iodine_pubsub_eng_s *m = iodine_pubsub_eng_get(self);
  fio_pubsub_attach(m->ptr);
  return self;
}

#define IODINE_PUBSUB_DEFAULT_NM "PUBSUB____DEFAULT"

static VALUE iodine_pubsub_eng_default_set(VALUE klass, VALUE eng) {
  fio_pubsub_engine_s *e = FIO_PUBSUB_CLUSTER;
  ID name = rb_intern(IODINE_PUBSUB_DEFAULT_NM);
  if (!IODINE_STORE_IS_SKIP(eng)) {
    e = iodine_pubsub_eng_get(eng)->ptr;
  }
  FIO_PUBSUB_DEFAULT = e;
  VALUE old = rb_const_get(iodine_rb_IODINE_BASE, name);
  if ((uintptr_t)old > 15)
    STORE.release(old);
  STORE.hold(eng);
  rb_const_remove(iodine_rb_IODINE_BASE, name);
  rb_const_set(iodine_rb_IODINE_BASE, name, eng);
  return eng;
  (void)klass;
}

static VALUE iodine_pubsub_eng_default_get(VALUE klass) {
  return rb_const_get(iodine_rb_IODINE_BASE,
                      rb_intern(IODINE_PUBSUB_DEFAULT_NM));
  (void)klass;
}

/**
 * Iodine::PubSub::Engine class instances are passed to subscription callbacks.
 */
static void Init_Iodine_PubSub_Engine(void) {
  /** Initialize Iodine::PubSub::Engine */
  rb_define_module_function(iodine_rb_IODINE_PUBSUB,
                            "default=",
                            iodine_pubsub_eng_default_set,
                            1);
  rb_define_module_function(iodine_rb_IODINE_PUBSUB,
                            "default",
                            iodine_pubsub_eng_default_get,
                            0);

  iodine_rb_IODINE_PUBSUB_ENG =
      rb_define_class_under(iodine_rb_IODINE_PUBSUB, "Engine", rb_cObject);
  STORE.hold(iodine_rb_IODINE_PUBSUB_ENG);
  rb_define_alloc_func(iodine_rb_IODINE_PUBSUB_ENG, iodine_pubsub_eng_alloc);

#define IODINE_PUBSUB_ENG_INTERNAL(name)                                       \
  do {                                                                         \
    VALUE tmp = rb_obj_alloc(iodine_rb_IODINE_PUBSUB_ENG);                     \
    iodine_pubsub_eng_get(tmp)->ptr = FIO_PUBSUB_##name;                       \
    rb_define_const(iodine_rb_IODINE_PUBSUB, #name, tmp);                      \
  } while (0)
  IODINE_PUBSUB_ENG_INTERNAL(ROOT);
  IODINE_PUBSUB_ENG_INTERNAL(PROCESS);
  IODINE_PUBSUB_ENG_INTERNAL(SIBLINGS);
  IODINE_PUBSUB_ENG_INTERNAL(LOCAL);
  IODINE_PUBSUB_ENG_INTERNAL(CLUSTER);
#undef IODINE_PUBSUB_ENG_INTERNAL

  rb_define_const(iodine_rb_IODINE_BASE,
                  IODINE_PUBSUB_DEFAULT_NM,
                  rb_const_get(iodine_rb_IODINE_PUBSUB, rb_intern("CLUSTER")));

  rb_define_method(iodine_rb_IODINE_PUBSUB_ENG,
                   "initialize",
                   iodine_pubsub_eng_initialize,
                   0);
}

#endif /* H___IODINE_PUBSUB_ENG___H */
