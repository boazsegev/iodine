#ifndef H___IODINE_REDIS___H
#define H___IODINE_REDIS___H

/* *****************************************************************************
Redis PubSub Engine Wrapper for Ruby

Exposes the facil.io Redis engine as Iodine::PubSub::Engine::Redis

Usage:
    redis = Iodine::PubSub::Engine::Redis.new("redis://localhost:6379/", ping: 50)
    Iodine::PubSub.default = redis
    redis.cmd("SET", "key", "value") { |result| puts result }

***************************************************************************** */

static VALUE iodine_rb_IODINE_REDIS;

/* *****************************************************************************
Redis Command Callback Context
***************************************************************************** */

typedef struct iodine_redis_cmd_ctx_s {
  VALUE block;
  fio_pubsub_engine_s *engine;
} iodine_redis_cmd_ctx_s;

/* *****************************************************************************
Redis Command Callback - Called from IO thread
***************************************************************************** */

typedef struct iodine_redis_callback_args_s {
  VALUE block;
  FIOBJ reply;
} iodine_redis_callback_args_s;

static void *iodine_redis_callback_in_gvl(void *arg_) {
  iodine_redis_callback_args_s *args = (iodine_redis_callback_args_s *)arg_;
  VALUE result = iodine_fiobj2ruby(args->reply);
  STORE.hold(result);
  iodine_ruby_call_inside(args->block, IODINE_CALL_ID, 1, &result);
  STORE.release(result);
  return NULL;
}

static void iodine_redis_cmd_callback(fio_pubsub_engine_s *e,
                                      FIOBJ reply,
                                      void *udata) {
  iodine_redis_cmd_ctx_s *ctx = (iodine_redis_cmd_ctx_s *)udata;
  if (ctx->block != Qnil) {
    iodine_redis_callback_args_s args = {
        .block = ctx->block,
        .reply = reply,
    };
    rb_thread_call_with_gvl(iodine_redis_callback_in_gvl, &args);
    STORE.release(ctx->block);
  }
  FIO_MEM_FREE(ctx, sizeof(*ctx));
  (void)e;
}

/* *****************************************************************************
Ruby Methods
***************************************************************************** */

/**
 * @!method initialize(url, ping: 0)
 * Creates a new Redis Pub/Sub engine.
 *
 * @param url [String] Redis server URL (e.g., "redis://localhost:6379/")
 * @param ping [Integer] Ping interval in seconds (0-255, default: 300)
 * @return [Iodine::PubSub::Engine::Redis]
 *
 * URL formats supported:
 * - "redis://host:port"
 * - "redis://user:password@host:port/db"
 * - "host:port"
 * - "host" (default port 6379)
 *
 * @example
 *   redis = Iodine::PubSub::Engine::Redis.new("redis://localhost:6379/")
 *   redis = Iodine::PubSub::Engine::Redis.new("redis://secret@host:6379/", ping: 60)
 */
static VALUE iodine_redis_initialize(int argc, VALUE *argv, VALUE self) {
  /* Get the engine struct from parent class */
  iodine_pubsub_eng_s *e = iodine_pubsub_eng_get(self);

  VALUE url_val;
  VALUE opts = Qnil;

  /* Parse arguments: url, optional keyword args */
  rb_scan_args(argc, argv, "1:", &url_val, &opts);

  /* Validate URL */
  if (url_val == Qnil || url_val == Qfalse) {
    rb_raise(rb_eArgError, "Redis URL is required");
    return Qnil;
  }
  Check_Type(url_val, T_STRING);
  const char *url = StringValueCStr(url_val);

  /* Parse ping interval from keyword args */
  uint8_t ping_interval = 0;
  if (opts != Qnil) {
    VALUE ping_val = rb_hash_aref(opts, ID2SYM(rb_intern("ping")));
    if (ping_val != Qnil) {
      long ping = NUM2LONG(ping_val);
      if (ping < 0 || ping > 255) {
        rb_raise(rb_eArgError, "ping must be between 0 and 255");
        return Qnil;
      }
      ping_interval = (uint8_t)ping;
    }
  }

  /* Create the Redis engine */
  e->ptr = fio_redis_new(.url = url, .ping_interval = ping_interval);
  if (!e->ptr) {
    rb_raise(rb_eRuntimeError, "Failed to create Redis engine");
    return Qnil;
  }

  /* Attach to pub/sub system */
  fio_pubsub_attach(e->ptr);

  return self;
}

/**
 * @!method cmd(*args, &block)
 * Sends a Redis command and optionally receives the response via callback.
 *
 * @param args [Array<String, Integer, Float, Symbol, Boolean, nil>] Command and arguments
 * @yield [result] Called with the Redis response
 * @yieldparam result [Object] The parsed Redis response
 * @return [Boolean] true on success, false on error
 *
 * @example
 *   redis.cmd("SET", "key", "value") { |r| puts "SET result: #{r}" }
 *   redis.cmd("GET", "key") { |value| puts "Value: #{value}" }
 *   redis.cmd("KEYS", "*") { |keys| p keys }
 *   redis.cmd("INCR", "counter") { |new_val| puts new_val }
 *
 * @note Do NOT use SUBSCRIBE/PSUBSCRIBE/UNSUBSCRIBE/PUNSUBSCRIBE commands.
 *       These are handled internally by the pub/sub system.
 */
static VALUE iodine_redis_cmd(int argc, VALUE *argv, VALUE self) {
  iodine_pubsub_eng_s *e = iodine_pubsub_eng_get(self);

  if (!e->ptr) {
    rb_raise(rb_eRuntimeError, "Redis engine not initialized");
    return Qfalse;
  }

  if (argc < 1) {
    rb_raise(rb_eArgError, "At least one argument (command) is required");
    return Qfalse;
  }

  /* Build FIOBJ array from Ruby arguments */
  FIOBJ cmd = fiobj_array_new();
  fiobj_array_reserve(cmd, argc);

  for (int i = 0; i < argc; ++i) {
    VALUE arg = argv[i];
    FIOBJ item = FIOBJ_INVALID;

    switch (rb_type(arg)) {
    case RUBY_T_STRING:
      item = fiobj_str_new_cstr(RSTRING_PTR(arg), RSTRING_LEN(arg));
      break;
    case RUBY_T_SYMBOL:
      arg = rb_sym2str(arg);
      item = fiobj_str_new_cstr(RSTRING_PTR(arg), RSTRING_LEN(arg));
      break;
    case RUBY_T_FIXNUM:
      item = fiobj_num_new((intptr_t)NUM2LL(arg));
      break;
    case RUBY_T_FLOAT: item = fiobj_float_new(RFLOAT_VALUE(arg)); break;
    case RUBY_T_TRUE: item = fiobj_str_new_cstr("1", 1); break;
    case RUBY_T_FALSE: item = fiobj_str_new_cstr("0", 1); break;
    case RUBY_T_NIL: item = fiobj_null(); break;
    default:
      /* Try to convert to string */
      arg = rb_funcallv(arg, IODINE_TO_S_ID, 0, NULL);
      if (RB_TYPE_P(arg, RUBY_T_STRING)) {
        item = fiobj_str_new_cstr(RSTRING_PTR(arg), RSTRING_LEN(arg));
      } else {
        fiobj_free(cmd);
        rb_raise(rb_eTypeError,
                 "Cannot convert argument %d to Redis command element",
                 i);
        return Qfalse;
      }
    }
    fiobj_array_push(cmd, item);
  }

  /* Get the block if provided */
  VALUE block = Qnil;
  if (rb_block_given_p()) {
    block = rb_block_proc();
  }

  /* Prepare callback context */
  iodine_redis_cmd_ctx_s *ctx = NULL;
  void (*callback)(fio_pubsub_engine_s *, FIOBJ, void *) = NULL;

  if (block != Qnil) {
    ctx =
        (iodine_redis_cmd_ctx_s *)FIO_MEM_REALLOC(NULL, 0, sizeof(*ctx), 0);
    if (!ctx) {
      fiobj_free(cmd);
      rb_raise(rb_eNoMemError, "Failed to allocate callback context");
      return Qfalse;
    }
    ctx->block = block;
    ctx->engine = e->ptr;
    STORE.hold(block);
    callback = iodine_redis_cmd_callback;
  }

  /* Send the command */
  int result = fio_redis_send(e->ptr, cmd, callback, ctx);
  fiobj_free(cmd);

  if (result != 0) {
    if (ctx) {
      STORE.release(block);
      FIO_MEM_FREE(ctx, sizeof(*ctx));
    }
    return Qfalse;
  }

  return Qtrue;
}

/* *****************************************************************************
Custom dealloc for Redis - need to call fio_redis_free instead of default
***************************************************************************** */

static void iodine_redis_free(void *ptr_) {
  iodine_pubsub_eng_s *e = (iodine_pubsub_eng_s *)ptr_;
  if (e->ptr) {
    fio_pubsub_detach(e->ptr);
    fio_redis_free(e->ptr);
    e->ptr = NULL;
  }
  ruby_xfree(e);
}

static const rb_data_type_t IODINE_REDIS_DATA_TYPE = {
    .wrap_struct_name = "IodineRedis",
    .function =
        {
            .dfree = iodine_redis_free,
            .dsize = iodine_pubsub_eng_data_size,
        },
    .parent = &IODINE_PUBSUB_ENG_DATA_TYPE,
    .data = NULL,
};

static VALUE iodine_redis_alloc(VALUE klass) {
  iodine_pubsub_eng_s *e =
      (iodine_pubsub_eng_s *)ruby_xmalloc(sizeof(iodine_pubsub_eng_s));
  if (!e)
    goto no_memory;
  *e = (iodine_pubsub_eng_s){0};
  return TypedData_Wrap_Struct(klass, &IODINE_REDIS_DATA_TYPE, e);

no_memory:
  FIO_LOG_FATAL("Memory allocation failed for Redis engine");
  fio_io_stop();
  return Qnil;
}

/* *****************************************************************************
Initialization
***************************************************************************** */

/**
 * Initializes the Iodine::PubSub::Engine::Redis class.
 */
static void Init_Iodine_Redis(void) {
  /* Define Iodine::PubSub::Engine::Redis as subclass of Engine */
  iodine_rb_IODINE_REDIS = rb_define_class_under(iodine_rb_IODINE_PUBSUB_ENG,
                                                  "Redis",
                                                  iodine_rb_IODINE_PUBSUB_ENG);
  STORE.hold(iodine_rb_IODINE_REDIS);

  /* Set up allocation function */
  rb_define_alloc_func(iodine_rb_IODINE_REDIS, iodine_redis_alloc);

  /* Define instance methods */
  rb_define_method(iodine_rb_IODINE_REDIS,
                   "initialize",
                   iodine_redis_initialize,
                   -1);
  rb_define_method(iodine_rb_IODINE_REDIS, "cmd", iodine_redis_cmd, -1);
}

#endif /* H___IODINE_REDIS___H */
