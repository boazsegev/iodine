#ifndef H___IODINE_PUBSUB_ENG___H
#define H___IODINE_PUBSUB_ENG___H
#include "iodine.h"

/* *****************************************************************************
Iodine PubSub Engine - Custom Publish/Subscribe Engine Support

This module provides the Iodine::PubSub::Engine Ruby class which allows
creating custom PubSub engines that can bridge Iodine's internal pub/sub
system with external message brokers (Redis, RabbitMQ, etc.).

A custom engine can implement any of these callbacks:
- subscribe(channel)     - Called when subscribing to a channel
- psubscribe(pattern)    - Called when subscribing to a pattern
- unsubscribe(channel)   - Called when unsubscribing from a channel
- punsubscribe(pattern)  - Called when unsubscribing from a pattern
- publish(message)       - Called when publishing a message
- on_cleanup             - Called when engine is detached

Built-in engines (constants on Iodine::PubSub):
- LOCAL    - Publish within the local machine (master + all workers)
- CLUSTER  - Publish to all workers across the cluster (default)

Ruby API:
- Iodine::PubSub.default = engine  - Set default engine
- Iodine::PubSub.default           - Get default engine
- Iodine::PubSub::Engine.new       - Create custom engine (subclass this)
***************************************************************************** */

/* *****************************************************************************
Ruby PubSub Engine Type
***************************************************************************** */

/**
 * Internal structure representing a PubSub engine.
 *
 * Wraps a facil.io pubsub engine with a Ruby handler object that
 * receives callbacks for subscribe/unsubscribe/publish operations.
 */
typedef struct iodine_pubsub_eng_s {
  fio_pubsub_engine_s engine; /**< The facil.io engine callbacks */
  fio_pubsub_engine_s *ptr;   /**< Pointer to engine (self or built-in) */
  VALUE handler;              /**< Ruby handler object (self) */
} iodine_pubsub_eng_s;

/* *****************************************************************************
Ruby PubSub Engine Bridge - C to Ruby Callback Wrappers

These functions bridge the facil.io C callbacks to Ruby method calls.
They handle GVL acquisition and Ruby object creation/cleanup.
***************************************************************************** */

/**
 * Arguments passed to GVL-wrapped callback functions.
 */
typedef struct iodine_pubsub_eng___args_s {
  iodine_pubsub_eng_s *eng;    /**< The engine receiving the callback */
  const fio_pubsub_msg_s *msg; /**< Message for publish callbacks */
  fio_buf_info_s channel;      /**< Channel name for subscribe callbacks */
  int16_t filter;              /**< Filter value (reserved) */
} iodine_pubsub_eng___args_s;

/**
 * Called after the engine was detached from the pubsub system.
 * Invokes the Ruby handler's `on_cleanup` method for resource cleanup.
 *
 * @param eng The detached engine
 */
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
/**
 * Subscribes to a channel. Called ONLY in the Root (master) process.
 * Invokes the Ruby handler's `subscribe` method with the channel name.
 *
 * @param eng The pubsub engine
 * @param channel The channel name to subscribe to
 * @param filter Filter value (reserved for future use)
 */
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
/**
 * Subscribes to a pattern. Called ONLY in the Root (master) process.
 * Invokes the Ruby handler's `psubscribe` method with the pattern.
 *
 * @param eng The pubsub engine
 * @param channel The pattern to subscribe to
 * @param filter Filter value (reserved for future use)
 */
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
/**
 * Unsubscribes from a channel. Called ONLY in the Root (master) process.
 * Invokes the Ruby handler's `unsubscribe` method with the channel name.
 *
 * @param eng The pubsub engine
 * @param channel The channel name to unsubscribe from
 * @param filter Filter value (reserved for future use)
 */
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
/**
 * Unsubscribes from a pattern. Called ONLY in the Root (master) process.
 * Invokes the Ruby handler's `punsubscribe` method with the pattern.
 *
 * @param eng The pubsub engine
 * @param channel The pattern to unsubscribe from
 * @param filter Filter value (reserved for future use)
 */
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

/**
 * Publishes a message through the engine. Called by any worker/thread.
 * Invokes the Ruby handler's `publish` method with a Message object.
 *
 * @param eng The pubsub engine
 * @param msg The message to publish
 */
static void iodine_pubsub_eng___publish(const fio_pubsub_engine_s *eng,
                                        const fio_pubsub_msg_s *msg) {
  iodine_pubsub_eng___args_s args = {
      .eng = (iodine_pubsub_eng_s *)eng,
      .msg = msg,
  };
  rb_thread_call_with_gvl(iodine_pubsub_eng___publish__in_GC, &args);
}

/**
 * Validates a Ruby object and creates a pubsub engine struct.
 *
 * Checks which callback methods the Ruby object responds to and
 * sets up the corresponding C callbacks. Methods not implemented
 * by the Ruby object will have NULL callbacks.
 *
 * @param obj The Ruby engine object to validate
 * @return A configured fio_pubsub_engine_s struct
 */
static fio_pubsub_engine_s iodine_pubsub___engine_validate(VALUE obj) {
  fio_pubsub_engine_s r = {
      /* Called after the engine was detached, may be used for cleanup. */
      .detached = (rb_respond_to(obj, rb_intern("on_cleanup")))
                      ? iodine_pubsub_eng___detached
                      : NULL,
      /* Subscribes to a channel. Called ONLY in the Root (master) process. */
      .subscribe = (rb_respond_to(obj, rb_intern("subscribe")))
                       ? iodine_pubsub_eng___subscribe
                       : NULL,
      /* Subscribes to a pattern. Called ONLY in the Root (master) process. */
      .psubscribe = (rb_respond_to(obj, rb_intern("psubscribe")))
                        ? iodine_pubsub_eng___psubscribe
                        : NULL,
      /* Unsubscribes from a channel. Called ONLY in the Root (master) process.
       */
      .unsubscribe = (rb_respond_to(obj, rb_intern("unsubscribe")))
                         ? iodine_pubsub_eng___unsubscribe
                         : NULL,
      /* Unsubscribes from a pattern. Called ONLY in the Root (master) process.
       */
      .punsubscribe = (rb_respond_to(obj, rb_intern("punsubscribe")))
                          ? iodine_pubsub_eng___punsubscribe
                          : NULL,
      /* Publishes a message through the engine. Called by any worker/thread. */
      .publish = (rb_respond_to(obj, rb_intern("publish")))
                     ? iodine_pubsub_eng___publish
                     : NULL,
  };
  return r;
}
/* *****************************************************************************
Ruby PubSub Engine Object - Ruby TypedData Wrapper
***************************************************************************** */

static size_t iodine_pubsub_eng_data_size(const void *ptr_) {
  iodine_pubsub_eng_s *m = (iodine_pubsub_eng_s *)ptr_;
  return sizeof(*m);
}

static void iodine_pubsub_eng_free(void *ptr_) {
  iodine_pubsub_eng_s *e = (iodine_pubsub_eng_s *)ptr_;
  if (fio_pubsub_engine_default() == e->ptr)
    fio_pubsub_engine_default_set(NULL);
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
Ruby Methods - Engine API
***************************************************************************** */

/**
 * Initializes a new PubSub engine and attaches it to the pubsub system.
 *
 * @param self The Engine instance
 * @return self
 *
 * Ruby: engine = Iodine::PubSub::Engine.new
 */
static VALUE iodine_pubsub_eng_initialize(VALUE self) {
  iodine_pubsub_eng_s *m = iodine_pubsub_eng_get(self);
  fio_pubsub_engine_attach(m->ptr);
  return self;
}

/** Internal constant name for storing the default engine */
#define IODINE_PUBSUB_DEFAULT_NM "PUBSUB____DEFAULT"

/**
 * Sets the default PubSub engine for all publish operations.
 *
 * @param klass The Iodine::PubSub module
 * @param eng The engine to set as default (or nil for CLUSTER)
 * @return The new default engine
 *
 * Ruby: Iodine::PubSub.default = my_engine
 */
static VALUE iodine_pubsub_eng_default_set(VALUE klass, VALUE eng) {
  fio_pubsub_engine_s *e = (fio_pubsub_engine_s *)fio_pubsub_engine_cluster();
  ID name = rb_intern(IODINE_PUBSUB_DEFAULT_NM);
  if (!IODINE_STORE_IS_SKIP(eng)) {
    e = iodine_pubsub_eng_get(eng)->ptr;
  }
  fio_pubsub_engine_default_set(e);
  VALUE old = rb_const_get(iodine_rb_IODINE_BASE, name);
  if ((uintptr_t)old > 15)
    STORE.release(old);
  STORE.hold(eng);
  rb_const_remove(iodine_rb_IODINE_BASE, name);
  rb_const_set(iodine_rb_IODINE_BASE, name, eng);
  return eng;
  (void)klass;
}

/**
 * Gets the current default PubSub engine.
 *
 * @param klass The Iodine::PubSub module
 * @return The current default engine
 *
 * Ruby: Iodine::PubSub.default
 */
static VALUE iodine_pubsub_eng_default_get(VALUE klass) {
  return rb_const_get(iodine_rb_IODINE_BASE,
                      rb_intern(IODINE_PUBSUB_DEFAULT_NM));
  (void)klass;
}

/* *****************************************************************************
Initialize - Ruby Class Registration
***************************************************************************** */

/**
 * Initializes the Iodine::PubSub::Engine Ruby class.
 *
 * Defines:
 * - Iodine::PubSub.default / default= module methods
 * - Iodine::PubSub::Engine class with initialize method
 * - Built-in engine constants: LOCAL and CLUSTER
 *
 * Only two built-in engines are provided:
 * - LOCAL (fio_pubsub_engine_ipc): Local machine only (master + all workers)
 * - CLUSTER (fio_pubsub_engine_cluster): Multi-machine cluster (default)
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

  /* Define LOCAL engine (IPC - local machine only) */
  {
    VALUE tmp = rb_obj_alloc(iodine_rb_IODINE_PUBSUB_ENG);
    iodine_pubsub_eng_get(tmp)->ptr =
        (fio_pubsub_engine_s *)fio_pubsub_engine_ipc();
    rb_define_const(iodine_rb_IODINE_PUBSUB, "LOCAL", tmp);
  }

  /* Define CLUSTER engine (multi-machine cluster) */
  {
    VALUE tmp = rb_obj_alloc(iodine_rb_IODINE_PUBSUB_ENG);
    iodine_pubsub_eng_get(tmp)->ptr =
        (fio_pubsub_engine_s *)fio_pubsub_engine_cluster();
    rb_define_const(iodine_rb_IODINE_PUBSUB, "CLUSTER", tmp);
  }

  rb_define_const(iodine_rb_IODINE_BASE,
                  IODINE_PUBSUB_DEFAULT_NM,
                  rb_const_get(iodine_rb_IODINE_PUBSUB, rb_intern("CLUSTER")));

  rb_define_method(iodine_rb_IODINE_PUBSUB_ENG,
                   "initialize",
                   iodine_pubsub_eng_initialize,
                   0);
}

/* *****************************************************************************
Iodine PubSub Subscription - Independent Non-IO Subscription Handle

This module provides the Iodine::PubSub::Subscription Ruby class which wraps
a non-IO-bound facil.io subscription. Unlike Iodine.subscribe (which allows
only one callback per channel per global context), each Subscription object
is its own independent context — multiple Subscription objects can subscribe
to the same channel simultaneously.

Features:
- Multiple independent subscriptions to the same channel
- Early cancellation via #cancel (idempotent)
- Live handler replacement via #handler=
- Auto-cancel on GC (via TypedData dfree)

Ruby API:
  sub = Iodine::PubSub::Subscription.new("channel") { |msg| ... }
  sub = Iodine::PubSub::Subscription.new(channel: "ch", filter: 0) { |msg| ... }
  sub.handler          # => Proc (or nil if cancelled)
  sub.handler = proc   # => proc (replaces callback for future messages)
  sub.active?          # => true/false
  sub.cancel           # => self (idempotent)
***************************************************************************** */

/* *****************************************************************************
Ruby PubSub Subscription Type
***************************************************************************** */

/**
 * Heap-allocated context passed as udata to the facil.io subscription.
 * Contains the proc AND a back-pointer to zero s->handle when unsubscribed.
 * Freed by on_unsubscribe after releasing the proc and zeroing the handle.
 */
typedef struct iodine_pubsub_sub_udata_s {
  VALUE proc;      /**< GC-protected Ruby Proc */
  uintptr_t *hptr; /**< pointer to iodine_pubsub_sub_s.handle (to zero it) */
} iodine_pubsub_sub_udata_s;

/**
 * Internal structure representing an independent pub/sub subscription.
 *
 * handle: facil.io subscription handle (0 = cancelled or already freed).
 * ud:     heap-allocated udata shared with facil.io (NULL after cancelled).
 * handler: Ruby-side getter/setter value.
 */
typedef struct iodine_pubsub_sub_s {
  uintptr_t handle;              /**< facil.io subscription handle (0 = done) */
  iodine_pubsub_sub_udata_s *ud; /**< shared udata (NULL once unsubscribed) */
  VALUE handler;                 /**< Ruby-side handler (getter/setter only) */
} iodine_pubsub_sub_s;

/* *****************************************************************************
Ruby PubSub Subscription - Message Callbacks
***************************************************************************** */

/**
 * Called inside the GVL to dispatch a pub/sub message to the Ruby handler.
 * udata is an iodine_pubsub_sub_udata_s* — reads proc from it.
 */
static void *iodine_pubsub_sub_on_message_in_gvl(void *m_) {
  fio_pubsub_msg_s *m = (fio_pubsub_msg_s *)m_;
  iodine_pubsub_sub_udata_s *ud = (iodine_pubsub_sub_udata_s *)m->udata;
  if (IODINE_STORE_IS_SKIP(ud->proc))
    return m_;
  VALUE msg = iodine_pubsub_msg_new(m);
  iodine_ruby_call_inside(ud->proc, IODINE_CALL_ID, 1, &msg);
  STORE.release(msg);
  return m_;
}

/**
 * Called by facil.io when a message arrives on the subscribed channel.
 */
static void iodine_pubsub_sub_on_message(fio_pubsub_msg_s *m) {
  rb_thread_call_with_gvl(iodine_pubsub_sub_on_message_in_gvl, m);
}

/**
 * Called by facil.io when the subscription is freed (cancel or reactor stop).
 * Releases the proc from STORE, zeroes the handle (if Ruby object still alive),
 * and frees the udata struct.
 * Safe to call outside GVL — STORE.release uses a mutex, no Ruby API.
 */
static void iodine_pubsub_sub_on_unsubscribe(void *udata) {
  iodine_pubsub_sub_udata_s *ud = (iodine_pubsub_sub_udata_s *)udata;
  STORE.release(ud->proc);
  if (ud->hptr)
    *ud->hptr = 0; /* zero s->handle so dfree knows it's already freed */
  ruby_xfree(ud);
}

/* *****************************************************************************
Ruby PubSub Subscription Object - Ruby TypedData Wrapper
***************************************************************************** */

static size_t iodine_pubsub_sub_data_size(const void *ptr_) {
  (void)ptr_;
  return sizeof(iodine_pubsub_sub_s);
}

/**
 * GC mark callback — keeps the handler proc alive while the subscription is.
 */
static void iodine_pubsub_sub_mark(void *ptr_) {
  iodine_pubsub_sub_s *s = (iodine_pubsub_sub_s *)ptr_;
  if (!IODINE_STORE_IS_SKIP(s->handler))
    rb_gc_mark(s->handler);
}

/**
 * TypedData free callback — called by Ruby GC when the Subscription is freed.
 *
 * If ud is non-NULL, null out ud->hptr so on_unsubscribe won't write to the
 * freed s->handle, then call fio_pubsub_unsubscribe to cancel and free udata.
 * If ud is NULL, on_unsubscribe already ran and cleaned everything up.
 */
static void iodine_pubsub_sub_free(void *ptr_) {
  iodine_pubsub_sub_s *s = (iodine_pubsub_sub_s *)ptr_;
  if (s->ud) {
    s->ud->hptr = NULL; /* prevent on_unsubscribe writing to freed s->handle */
    fio_pubsub_unsubscribe(.subscription_handle_ptr = &s->handle);
  }
  ruby_xfree(s);
  FIO_LEAK_COUNTER_ON_FREE(iodine_pubsub_sub);
}

static const rb_data_type_t IODINE_PUBSUB_SUB_DATA_TYPE = {
    .wrap_struct_name = "IodinePSSub",
    .function =
        {
            .dmark = iodine_pubsub_sub_mark,
            .dfree = iodine_pubsub_sub_free,
            .dsize = iodine_pubsub_sub_data_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE iodine_pubsub_sub_alloc(VALUE klass) {
  iodine_pubsub_sub_s *s = (iodine_pubsub_sub_s *)ruby_xmalloc(sizeof(*s));
  if (!s)
    goto no_memory;
  *s = (iodine_pubsub_sub_s){.handle = 0, .ud = NULL, .handler = Qnil};
  FIO_LEAK_COUNTER_ON_ALLOC(iodine_pubsub_sub);
  return TypedData_Wrap_Struct(klass, &IODINE_PUBSUB_SUB_DATA_TYPE, s);
no_memory:
  FIO_LOG_FATAL("Memory allocation failed");
  fio_io_stop();
  return Qnil;
}

static iodine_pubsub_sub_s *iodine_pubsub_sub_get(VALUE self) {
  iodine_pubsub_sub_s *s;
  TypedData_Get_Struct(self,
                       iodine_pubsub_sub_s,
                       &IODINE_PUBSUB_SUB_DATA_TYPE,
                       s);
  return s;
}

/* *****************************************************************************
Ruby Methods - Subscription API
***************************************************************************** */

/**
 * Initializes a new independent pub/sub subscription.
 *
 * Creates a non-IO-bound facil.io subscription using subscription_handle_ptr,
 * allowing multiple independent subscriptions to the same channel.
 *
 * @param channel [String] the channel name to subscribe to
 * @param filter [Integer] optional numerical filter (default: 0)
 * @param since [Integer] optional replay-since timestamp in milliseconds
 * @param block [Proc] required message handler callback
 * @return self
 *
 * Ruby: sub = Iodine::PubSub::Subscription.new("channel") { |msg| ... }
 *       sub = Iodine::PubSub::Subscription.new(channel: "ch", filter: 0) {
 * |msg| ... }
 */
static VALUE iodine_pubsub_sub_initialize(int argc, VALUE *argv, VALUE self) {
  iodine_pubsub_sub_s *s = iodine_pubsub_sub_get(self);
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  int64_t filter = 0;
  uint64_t since = 0;
  VALUE proc = Qnil;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(channel, 0, "channel", 1),
                  IODINE_ARG_NUM(filter, 0, "filter", 0),
                  IODINE_ARG_U64(since, 0, "since", 0),
                  IODINE_ARG_PROC(proc, 0, "callback", 1));

  if ((size_t)filter & (~(size_t)0xFFFF))
    rb_raise(rb_eRangeError,
             "filter out of range (%lld > 0xFFFF)",
             (long long)filter);

  /* Allocate shared udata — owns the GC-protected proc and the hptr backlink */
  iodine_pubsub_sub_udata_s *ud =
      (iodine_pubsub_sub_udata_s *)ruby_xmalloc(sizeof(*ud));
  if (!ud)
    rb_raise(rb_eNoMemError, "Subscription: udata allocation failed");
  STORE.hold(proc);
  *ud = (iodine_pubsub_sub_udata_s){.proc = proc, .hptr = &s->handle};
  s->ud = ud;
  s->handler = proc;

  /* Subscribe with subscription_handle_ptr (no IO binding).
   * udata = ud (heap struct). on_unsubscribe zeroes handle, releases proc,
   * and frees ud — safe whether called by cancel, dfree, or reactor stop. */
  fio_pubsub_subscribe(.subscription_handle_ptr = &s->handle,
                       .filter = (int16_t)filter,
                       .channel = channel,
                       .udata = (void *)ud,
                       .queue = fio_io_async_queue(&IODINE_THREAD_POOL),
                       .on_message = iodine_pubsub_sub_on_message,
                       .on_unsubscribe = iodine_pubsub_sub_on_unsubscribe,
                       .replay_since = since);
  return self;
}

/**
 * Returns the current message handler proc.
 *
 * @return [Proc, nil] the handler proc, or nil if cancelled
 *
 * Ruby: sub.handler  # => Proc
 */
static VALUE iodine_pubsub_sub_handler_get(VALUE self) {
  iodine_pubsub_sub_s *s = iodine_pubsub_sub_get(self);
  return s->handler;
}

/**
 * Replaces the message handler proc.
 *
 * Updates both the Ruby-side getter (#handler) and the udata proc used for
 * message delivery. After handler=, future messages will be dispatched to
 * the new proc.
 *
 * Thread-safety: called inside GVL; udata proc is also read inside GVL
 * (in iodine_pubsub_sub_on_message_in_gvl), so the update is safe.
 *
 * @param new_handler [Proc] the new handler (must respond to #call)
 * @return [Proc] the new handler
 *
 * Ruby: sub.handler = proc { |msg| ... }
 */
static VALUE iodine_pubsub_sub_handler_set(VALUE self, VALUE new_handler) {
  iodine_pubsub_sub_s *s = iodine_pubsub_sub_get(self);
  if (!IODINE_STORE_IS_SKIP(new_handler) &&
      !rb_respond_to(new_handler, rb_intern2("call", 4)))
    rb_raise(rb_eArgError, "handler must respond to `call`");
  STORE.hold(new_handler);
  /* Update the udata proc so future message delivery uses the new handler.
   * Both this method and iodine_pubsub_sub_on_message_in_gvl run inside the
   * GVL, so this assignment is thread-safe. The old proc is released once —
   * either via ud->proc (if active) or via s->handler (if cancelled). */
  if (s->ud) {
    VALUE old_proc = s->ud->proc;
    s->ud->proc = new_handler;
    s->handler = new_handler;
    if (!IODINE_STORE_IS_SKIP(old_proc))
      STORE.release(old_proc);
  } else {
    VALUE old = s->handler;
    s->handler = new_handler;
    if (!IODINE_STORE_IS_SKIP(old))
      STORE.release(old);
  }
  return new_handler;
}

/**
 * Returns true if the subscription is still active.
 *
 * @return [Boolean] true if active, false if cancelled or not yet subscribed
 *
 * Ruby: sub.active?  # => true/false
 */
static VALUE iodine_pubsub_sub_active_p(VALUE self) {
  iodine_pubsub_sub_s *s = iodine_pubsub_sub_get(self);
  return s->ud ? Qtrue : Qfalse;
}

/**
 * Cancels the subscription early. Idempotent — safe to call multiple times.
 *
 * After cancellation, no further messages will be delivered. The handler
 * proc is NOT released here (it remains accessible via #handler until GC).
 *
 * @return [self]
 *
 * Ruby: sub.cancel  # => self
 */
static VALUE iodine_pubsub_sub_cancel(VALUE self) {
  iodine_pubsub_sub_s *s = iodine_pubsub_sub_get(self);
  if (!s->ud)
    return self;      /* already cancelled — idempotent */
  s->ud->hptr = NULL; /* prevent on_unsubscribe from writing to s->handle */
  fio_pubsub_unsubscribe(.subscription_handle_ptr = &s->handle);
  s->ud = NULL; /* mark as cancelled so dfree won't call unsubscribe again */
  return self;
}

/* *****************************************************************************
Initialize - Ruby Class Registration
***************************************************************************** */

/**
 * Initializes the Iodine::PubSub::Subscription Ruby class.
 *
 * Defines:
 * - Iodine::PubSub::Subscription class with initialize, handler, handler=,
 *   active?, and cancel methods.
 *
 * Each Subscription instance is an independent non-IO-bound subscription,
 * allowing multiple simultaneous subscriptions to the same channel.
 */
static void Init_Iodine_PubSub_Subscription(void) {
  iodine_rb_IODINE_PUBSUB_SUB = rb_define_class_under(iodine_rb_IODINE_PUBSUB,
                                                      "Subscription",
                                                      rb_cObject);
  STORE.hold(iodine_rb_IODINE_PUBSUB_SUB);
  rb_define_alloc_func(iodine_rb_IODINE_PUBSUB_SUB, iodine_pubsub_sub_alloc);

  rb_define_method(iodine_rb_IODINE_PUBSUB_SUB,
                   "initialize",
                   iodine_pubsub_sub_initialize,
                   -1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_SUB,
                   "handler",
                   iodine_pubsub_sub_handler_get,
                   0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_SUB,
                   "handler=",
                   iodine_pubsub_sub_handler_set,
                   1);
  rb_define_method(iodine_rb_IODINE_PUBSUB_SUB,
                   "active?",
                   iodine_pubsub_sub_active_p,
                   0);
  rb_define_method(iodine_rb_IODINE_PUBSUB_SUB,
                   "cancel",
                   iodine_pubsub_sub_cancel,
                   0);
}

#endif /* H___IODINE_PUBSUB_ENG___H */
