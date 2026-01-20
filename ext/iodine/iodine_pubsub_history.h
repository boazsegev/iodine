#ifndef H___IODINE_PUBSUB_HISTORY___H
#define H___IODINE_PUBSUB_HISTORY___H
#include "iodine.h"

/* *****************************************************************************
Iodine PubSub History - Message History and Replay Support

This module provides the Iodine::PubSub::History Ruby module which allows
enabling message history caching and replay for the pub/sub system.

When history is enabled, published messages are cached in memory (up to a
configurable size limit). Subscribers can then request replay of missed
messages by providing a `since` timestamp when subscribing.

Ruby API:

  Iodine::PubSub::History.cache(size_limit: 256 * 1024 * 1024)
    - Enables the built-in in-memory history cache
    - size_limit: Maximum cache size in bytes (default: 256MB)
    - Returns true on success
    - Memory cache has highest priority (255) for fastest replay

  Iodine::PubSub::History.cache?
    - Returns true if memory caching is enabled

Usage Example:

  # Enable memory cache with 128MB limit
  Iodine::PubSub::History.cache(size_limit: 128 * 1024 * 1024)

  # Subscribe with history replay (get messages from last 60 seconds)
  since_ms = (Time.now.to_i - 60) * 1000
  Iodine.subscribe(channel: "chat", since: since_ms) do |msg|
    puts "Message: #{msg.message}"
  end

Custom History Managers:

For advanced use cases (e.g., persistent storage, Redis-backed history),
you can create custom history managers by subclassing
Iodine::PubSub::History::Manager and implementing:

  - push(message)     - Store a message in history
  - replay(channel:, filter:, since:, &block) - Replay messages
  - oldest(channel:, filter:) - Get oldest available timestamp

***************************************************************************** */

/* Ruby class for Iodine::PubSub::History module */
static VALUE iodine_rb_IODINE_PUBSUB_HISTORY;

/* Track if built-in cache is enabled */
static uint8_t iodine_pubsub_history_cache_enabled = 0;

/* *****************************************************************************
Ruby Methods - History API
***************************************************************************** */

/**
 * Enables the built-in in-memory history cache.
 *
 * @param argc Argument count
 * @param argv Arguments array
 * @param self The History module
 * @return Qtrue on success
 *
 * Ruby: Iodine::PubSub::History.cache(size_limit: 256 * 1024 * 1024)
 */
static VALUE iodine_pubsub_history_cache(int argc, VALUE *argv, VALUE self) {
  size_t size_limit = 0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_SIZE_T(size_limit, 0, "size_limit", 0));

  /* Get the built-in cache manager (initializes with size_limit) */
  const fio_pubsub_history_s *mgr = fio_pubsub_history_cache(size_limit);
  if (!mgr) {
    rb_raise(rb_eRuntimeError, "Failed to initialize history cache");
    return Qfalse;
  }

  /* Attach with highest priority (255) - memory cache should be fastest */
  if (fio_pubsub_history_attach(mgr, 255) != 0) {
    rb_raise(rb_eRuntimeError, "Failed to attach history cache");
    return Qfalse;
  }

  iodine_pubsub_history_cache_enabled = 1;
  return Qtrue;
  (void)self;
}

/**
 * Returns true if the built-in memory cache is enabled.
 *
 * @param self The History module
 * @return Qtrue if cache is enabled, Qfalse otherwise
 *
 * Ruby: Iodine::PubSub::History.cache?
 */
static VALUE iodine_pubsub_history_cache_p(VALUE self) {
  return iodine_pubsub_history_cache_enabled ? Qtrue : Qfalse;
  (void)self;
}

/* *****************************************************************************
Custom History Manager - Ruby TypedData Wrapper

Allows Ruby subclasses to implement custom history storage backends.
***************************************************************************** */

/**
 * Internal structure representing a custom PubSub history manager.
 *
 * Wraps a facil.io pubsub history manager with a Ruby handler object that
 * receives callbacks for push/replay/oldest operations.
 */
typedef struct iodine_pubsub_history_s {
  fio_pubsub_history_s manager; /**< The facil.io history manager callbacks */
  VALUE handler;                /**< Ruby handler object (self) */
  uint8_t priority;             /**< Manager priority (0-255) */
  uint8_t attached;             /**< Whether manager is attached */
} iodine_pubsub_history_s;

/**
 * Arguments passed to GVL-wrapped callback functions.
 */
typedef struct iodine_pubsub_history___args_s {
  iodine_pubsub_history_s *hist; /**< The history manager */
  fio_pubsub_msg_s *msg;         /**< Message for push callbacks */
  fio_buf_info_s channel;        /**< Channel name */
  int16_t filter;                /**< Filter value */
  uint64_t since;                /**< Timestamp for replay */
  void (*on_message)(fio_pubsub_msg_s *msg, void *udata);
  void (*on_done)(void *udata);
  void *udata;
  int result;             /**< Return value from callback */
  uint64_t oldest_result; /**< Return value for oldest() */
} iodine_pubsub_history___args_s;

/* Forward declarations */
static void iodine_pubsub_history___detached(const fio_pubsub_history_s *hist);
static int iodine_pubsub_history___push(const fio_pubsub_history_s *hist,
                                        fio_pubsub_msg_s *msg);
static int iodine_pubsub_history___replay(
    const fio_pubsub_history_s *hist,
    fio_buf_info_s channel,
    int16_t filter,
    uint64_t since,
    void (*on_message)(fio_pubsub_msg_s *msg, void *udata),
    void (*on_done)(void *udata),
    void *udata);
static uint64_t iodine_pubsub_history___oldest(const fio_pubsub_history_s *hist,
                                               fio_buf_info_s channel,
                                               int16_t filter);

/**
 * Called after the history manager was detached.
 * Invokes the Ruby handler's `on_cleanup` method for resource cleanup.
 */
static void iodine_pubsub_history___detached(const fio_pubsub_history_s *hist) {
  iodine_pubsub_history_s *h = (iodine_pubsub_history_s *)hist;
  h->attached = 0;
  iodine_ruby_call_outside(h->handler, rb_intern("on_cleanup"));
}

static void *iodine_pubsub_history___push__in_GC(void *a_) {
  iodine_pubsub_history___args_s *args = (iodine_pubsub_history___args_s *)a_;
  VALUE msg = iodine_pubsub_msg_new(args->msg);
  iodine_caller_result_s r =
      iodine_ruby_call_inside(args->hist->handler, rb_intern("push"), 1, &msg);
  STORE.release(msg);
  args->result = (r.exception || r.result == Qfalse) ? -1 : 0;
  return NULL;
}

/**
 * Stores a message in history.
 * Invokes the Ruby handler's `push` method with the message.
 */
static int iodine_pubsub_history___push(const fio_pubsub_history_s *hist,
                                        fio_pubsub_msg_s *msg) {
  iodine_pubsub_history___args_s args = {
      .hist = (iodine_pubsub_history_s *)hist,
      .msg = msg,
      .result = -1,
  };
  rb_thread_call_with_gvl(iodine_pubsub_history___push__in_GC, &args);
  return args.result;
}

/* Replay callback wrapper - called for each message during replay */
typedef struct {
  VALUE callback;
  void (*on_message)(fio_pubsub_msg_s *msg, void *udata);
  void (*on_done)(void *udata);
  void *udata;
} iodine_pubsub_history_replay_ctx_s;

static VALUE iodine_pubsub_history___replay_each(VALUE msg_rb, VALUE ctx_) {
  iodine_pubsub_history_replay_ctx_s *ctx =
      (iodine_pubsub_history_replay_ctx_s *)ctx_;
  /* Convert Ruby message back to C message for the callback */
  iodine_pubsub_msg_wrapper_s *wrapper = iodine_pubsub_msg_get(msg_rb);
  if (wrapper && ctx->on_message) {
    /* Create a temporary message struct from the Ruby wrapper */
    fio_pubsub_msg_s msg = {
        .id = wrapper->store[IODINE_PUBSUB_MSG_STORE_id] != Qnil
                  ? NUM2ULL(wrapper->store[IODINE_PUBSUB_MSG_STORE_id])
                  : 0,
        .timestamp =
            wrapper->store[IODINE_PUBSUB_MSG_STORE_published] != Qnil
                ? NUM2ULL(wrapper->store[IODINE_PUBSUB_MSG_STORE_published])
                : 0,
        .filter = wrapper->store[IODINE_PUBSUB_MSG_STORE_filter] != Qnil
                      ? (int16_t)NUM2INT(
                            wrapper->store[IODINE_PUBSUB_MSG_STORE_filter])
                      : 0,
    };
    if (wrapper->store[IODINE_PUBSUB_MSG_STORE_channel] != Qnil) {
      msg.channel = FIO_BUF_INFO2(
          RSTRING_PTR(wrapper->store[IODINE_PUBSUB_MSG_STORE_channel]),
          (size_t)RSTRING_LEN(wrapper->store[IODINE_PUBSUB_MSG_STORE_channel]));
    }
    if (wrapper->store[IODINE_PUBSUB_MSG_STORE_message] != Qnil) {
      msg.message = FIO_BUF_INFO2(
          RSTRING_PTR(wrapper->store[IODINE_PUBSUB_MSG_STORE_message]),
          (size_t)RSTRING_LEN(wrapper->store[IODINE_PUBSUB_MSG_STORE_message]));
    }
    ctx->on_message(&msg, ctx->udata);
  }
  return Qnil;
}

static void *iodine_pubsub_history___replay__in_GC(void *a_) {
  iodine_pubsub_history___args_s *args = (iodine_pubsub_history___args_s *)a_;
  VALUE argv[3];
  argv[0] = args->channel.len ? rb_str_new(args->channel.buf, args->channel.len)
                              : Qnil;
  argv[1] = INT2NUM(args->filter);
  argv[2] = ULL2NUM(args->since);

  /* Create a context for the replay iteration */
  iodine_pubsub_history_replay_ctx_s ctx = {
      .on_message = args->on_message,
      .on_done = args->on_done,
      .udata = args->udata,
  };

  /* Call Ruby replay method with named args */
  VALUE kwargs = rb_hash_new();
  rb_hash_aset(kwargs, ID2SYM(rb_intern("channel")), argv[0]);
  rb_hash_aset(kwargs, ID2SYM(rb_intern("filter")), argv[1]);
  rb_hash_aset(kwargs, ID2SYM(rb_intern("since")), argv[2]);

  VALUE rb_args[1] = {kwargs};
  iodine_caller_result_s r = iodine_ruby_call_inside(args->hist->handler,
                                                     rb_intern("replay"),
                                                     1,
                                                     rb_args);

  if (r.exception) {
    args->result = -1;
  } else if (r.result == Qnil || r.result == Qfalse) {
    args->result = -1;
  } else if (RB_TYPE_P(r.result, RUBY_T_ARRAY)) {
    /* Iterate over returned messages */
    for (long i = 0; i < RARRAY_LEN(r.result); ++i) {
      iodine_pubsub_history___replay_each(RARRAY_AREF(r.result, i),
                                          (VALUE)&ctx);
    }
    args->result = 0;
  } else {
    args->result = 0;
  }

  /* Always call on_done */
  if (args->on_done)
    args->on_done(args->udata);

  return NULL;
}

/**
 * Replays messages since a timestamp.
 * Invokes the Ruby handler's `replay` method.
 */
static int iodine_pubsub_history___replay(
    const fio_pubsub_history_s *hist,
    fio_buf_info_s channel,
    int16_t filter,
    uint64_t since,
    void (*on_message)(fio_pubsub_msg_s *msg, void *udata),
    void (*on_done)(void *udata),
    void *udata) {
  iodine_pubsub_history___args_s args = {
      .hist = (iodine_pubsub_history_s *)hist,
      .channel = channel,
      .filter = filter,
      .since = since,
      .on_message = on_message,
      .on_done = on_done,
      .udata = udata,
      .result = -1,
  };
  rb_thread_call_with_gvl(iodine_pubsub_history___replay__in_GC, &args);
  return args.result;
}

static void *iodine_pubsub_history___oldest__in_GC(void *a_) {
  iodine_pubsub_history___args_s *args = (iodine_pubsub_history___args_s *)a_;
  VALUE argv[2];
  argv[0] = args->channel.len ? rb_str_new(args->channel.buf, args->channel.len)
                              : Qnil;
  argv[1] = INT2NUM(args->filter);

  VALUE kwargs = rb_hash_new();
  rb_hash_aset(kwargs, ID2SYM(rb_intern("channel")), argv[0]);
  rb_hash_aset(kwargs, ID2SYM(rb_intern("filter")), argv[1]);

  VALUE rb_args[1] = {kwargs};
  iodine_caller_result_s r = iodine_ruby_call_inside(args->hist->handler,
                                                     rb_intern("oldest"),
                                                     1,
                                                     rb_args);

  if (r.exception || r.result == Qnil) {
    args->oldest_result = UINT64_MAX;
  } else if (RB_TYPE_P(r.result, RUBY_T_FIXNUM)) {
    args->oldest_result = NUM2ULL(r.result);
  } else {
    args->oldest_result = UINT64_MAX;
  }
  return NULL;
}

/**
 * Gets the oldest available timestamp for a channel.
 * Invokes the Ruby handler's `oldest` method.
 */
static uint64_t iodine_pubsub_history___oldest(const fio_pubsub_history_s *hist,
                                               fio_buf_info_s channel,
                                               int16_t filter) {
  iodine_pubsub_history___args_s args = {
      .hist = (iodine_pubsub_history_s *)hist,
      .channel = channel,
      .filter = filter,
      .oldest_result = UINT64_MAX,
  };
  rb_thread_call_with_gvl(iodine_pubsub_history___oldest__in_GC, &args);
  return args.oldest_result;
}

/**
 * Validates a Ruby object and creates a history manager struct.
 */
static fio_pubsub_history_s iodine_pubsub___history_validate(VALUE obj) {
  fio_pubsub_history_s r = {
      .detached = (rb_respond_to(obj, rb_intern("on_cleanup")))
                      ? iodine_pubsub_history___detached
                      : NULL,
      .push = (rb_respond_to(obj, rb_intern("push")))
                  ? iodine_pubsub_history___push
                  : NULL,
      .replay = (rb_respond_to(obj, rb_intern("replay")))
                    ? iodine_pubsub_history___replay
                    : NULL,
      .oldest = (rb_respond_to(obj, rb_intern("oldest")))
                    ? iodine_pubsub_history___oldest
                    : NULL,
  };
  return r;
}

/* *****************************************************************************
Ruby History Manager Object - Ruby TypedData Wrapper
***************************************************************************** */

static size_t iodine_pubsub_history_data_size(const void *ptr_) {
  iodine_pubsub_history_s *h = (iodine_pubsub_history_s *)ptr_;
  return sizeof(*h);
  (void)h;
}

static void iodine_pubsub_history_free(void *ptr_) {
  iodine_pubsub_history_s *h = (iodine_pubsub_history_s *)ptr_;
  if (h->attached)
    fio_pubsub_history_detach(&h->manager);
  ruby_xfree(h);
}

static const rb_data_type_t IODINE_PUBSUB_HISTORY_DATA_TYPE = {
    .wrap_struct_name = "IodinePSHistory",
    .function =
        {
            .dfree = iodine_pubsub_history_free,
            .dsize = iodine_pubsub_history_data_size,
        },
    .data = NULL,
};

static VALUE iodine_pubsub_history_alloc(VALUE klass) {
  iodine_pubsub_history_s *h =
      (iodine_pubsub_history_s *)ruby_xmalloc(sizeof(*h));
  if (!h)
    goto no_memory;
  *h = (iodine_pubsub_history_s){0};
  h->handler =
      TypedData_Wrap_Struct(klass, &IODINE_PUBSUB_HISTORY_DATA_TYPE, h);
  h->manager = iodine_pubsub___history_validate(h->handler);
  return h->handler;

no_memory:
  FIO_LOG_FATAL("Memory allocation failed");
  fio_io_stop();
  return Qnil;
}

static iodine_pubsub_history_s *iodine_pubsub_history_get(VALUE self) {
  iodine_pubsub_history_s *h;
  TypedData_Get_Struct(self,
                       iodine_pubsub_history_s,
                       &IODINE_PUBSUB_HISTORY_DATA_TYPE,
                       h);
  return h;
}

/* *****************************************************************************
Ruby Methods - Manager API
***************************************************************************** */

/**
 * Initializes a new History manager and attaches it to the pubsub system.
 *
 * @param argc Argument count
 * @param argv Arguments (priority: 0-255, default 128)
 * @param self The Manager instance
 * @return self
 *
 * Ruby: manager = Iodine::PubSub::History::Manager.new(priority: 128)
 */
static VALUE iodine_pubsub_history_manager_initialize(int argc,
                                                      VALUE *argv,
                                                      VALUE self) {
  iodine_pubsub_history_s *h = iodine_pubsub_history_get(self);
  uint8_t priority = 128;

  iodine_rb2c_arg(argc, argv, IODINE_ARG_U8(priority, 0, "priority", 0));

  h->priority = priority;
  if (fio_pubsub_history_attach(&h->manager, priority) != 0) {
    rb_raise(rb_eRuntimeError, "Failed to attach history manager");
    return Qnil;
  }
  h->attached = 1;
  return self;
}

/**
 * Detaches the history manager from the pubsub system.
 *
 * @param self The Manager instance
 * @return self
 *
 * Ruby: manager.detach
 */
static VALUE iodine_pubsub_history_manager_detach(VALUE self) {
  iodine_pubsub_history_s *h = iodine_pubsub_history_get(self);
  if (h->attached) {
    fio_pubsub_history_detach(&h->manager);
    h->attached = 0;
  }
  return self;
}

/**
 * Returns true if the manager is attached.
 *
 * @param self The Manager instance
 * @return Qtrue if attached, Qfalse otherwise
 *
 * Ruby: manager.attached?
 */
static VALUE iodine_pubsub_history_manager_attached_p(VALUE self) {
  iodine_pubsub_history_s *h = iodine_pubsub_history_get(self);
  return h->attached ? Qtrue : Qfalse;
}

/* *****************************************************************************
Initialize - Ruby Class Registration
***************************************************************************** */

/**
 * Initializes the Iodine::PubSub::History Ruby module.
 *
 * Defines:
 * - Iodine::PubSub::History.cache(size_limit:) - Enable built-in memory cache
 * - Iodine::PubSub::History.cache? - Check if memory cache is enabled
 * - Iodine::PubSub::History::Manager - Custom history manager base class
 */
static void Init_Iodine_PubSub_History(void) {
  /* Initialize Iodine::PubSub::History module */
  iodine_rb_IODINE_PUBSUB_HISTORY =
      rb_define_module_under(iodine_rb_IODINE_PUBSUB, "History");
  STORE.hold(iodine_rb_IODINE_PUBSUB_HISTORY);

  rb_define_module_function(iodine_rb_IODINE_PUBSUB_HISTORY,
                            "cache",
                            iodine_pubsub_history_cache,
                            -1);
  rb_define_module_function(iodine_rb_IODINE_PUBSUB_HISTORY,
                            "cache?",
                            iodine_pubsub_history_cache_p,
                            0);

  /* Initialize Iodine::PubSub::History::Manager class */
  VALUE manager_class = rb_define_class_under(iodine_rb_IODINE_PUBSUB_HISTORY,
                                              "Manager",
                                              rb_cObject);
  STORE.hold(manager_class);
  rb_define_alloc_func(manager_class, iodine_pubsub_history_alloc);

  rb_define_method(manager_class,
                   "initialize",
                   iodine_pubsub_history_manager_initialize,
                   -1);
  rb_define_method(manager_class,
                   "detach",
                   iodine_pubsub_history_manager_detach,
                   0);
  rb_define_method(manager_class,
                   "attached?",
                   iodine_pubsub_history_manager_attached_p,
                   0);
}

#endif /* H___IODINE_PUBSUB_HISTORY___H */
