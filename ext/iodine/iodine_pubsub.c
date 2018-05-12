#include "iodine_pubsub.h"
#include "iodine_fiobj2rb.h"
#include "pubsub.h"
#include "redis_engine.h"

/*
NOTE:

This file defines Pub/Sub management and settings, not Pub/Sub usage.

This file doen't include the `Iodine.subscribe`, `Iodine.unsubscribe` and
`Iodine.publish` methods.

These methods are all defined in the Connection module (iodine_connection.h).
*/

/* *****************************************************************************
static consts
***************************************************************************** */

static ID subscribe_id;
static ID unsubscribe_id;
static ID publish_id;
static ID default_id;
static ID redis_id;
static ID call_id;

/**
The {Iodine::PubSub::Engine} class is the parent for all engines to inherit
from.

Engines should inherit this class and override the `subscribe`, `unsubscribe`
and `publish` callbacks (which shall be called by {Iodine}).

After creation, Engines should attach themselves to Iodine using
{Iodine::PubSub.attach} or their callbacks will never get called.

Engines can also set themselves to be the default engine using
{Iodine::PubSub.default=}.
*/
static VALUE EngineClass;

/* *****************************************************************************
Ruby <=> C Callbacks
***************************************************************************** */

typedef struct {
  iodine_pubsub_s *eng;
  FIOBJ ch;
  FIOBJ msg;
  uint8_t pattern;
} iodine_pubsub_task_s;

#define iodine_engine(eng) ((iodine_pubsub_s *)(eng))

/* calls an engine's `subscribe` callback within the GVL */
static void *iodine_pubsub_GIL_subscribe(void *tsk_) {
  iodine_pubsub_task_s *task = tsk_;
  VALUE args[2];
  fio_cstr_s tmp = fiobj_obj2cstr(task->ch);
  args[0] = rb_str_new(tmp.data, tmp.len);
  args[1] = task->pattern ? Qtrue : Qnil; // TODO: Qtrue should be :redis
  IodineCaller.call2(task->eng->handler, subscribe_id, 2, args);
  return NULL;
}

/** Must subscribe channel. Failures are ignored. */
static void iodine_pubsub_on_subscribe(const pubsub_engine_s *eng,
                                       FIOBJ channel, uint8_t use_pattern) {
  if (iodine_engine(eng)->handler == Qnil) {
    return;
  }
  iodine_pubsub_task_s task = {
      .eng = iodine_engine(eng), .ch = channel, .pattern = use_pattern};
  IodineCaller.enterGVL(iodine_pubsub_GIL_subscribe, &task);
}

/* calls an engine's `unsubscribe` callback within the GVL */
static void *iodine_pubsub_GIL_unsubscribe(void *tsk_) {
  iodine_pubsub_task_s *task = tsk_;
  VALUE args[2];
  fio_cstr_s tmp = fiobj_obj2cstr(task->ch);
  args[0] = rb_str_new(tmp.data, tmp.len);
  args[1] = task->pattern ? Qtrue : Qnil; // TODO: Qtrue should be :redis
  IodineCaller.call2(task->eng->handler, unsubscribe_id, 2, args);
  return NULL;
}

/** Must unsubscribe channel. Failures are ignored. */
static void iodine_pubsub_on_unsubscribe(const pubsub_engine_s *eng,
                                         FIOBJ channel, uint8_t use_pattern) {
  if (iodine_engine(eng)->handler == Qnil) {
    return;
  }
  iodine_pubsub_task_s task = {
      .eng = iodine_engine(eng), .ch = channel, .pattern = use_pattern};
  IodineCaller.enterGVL(iodine_pubsub_GIL_unsubscribe, &task);
}

/* calls an engine's `unsubscribe` callback within the GVL */
static void *iodine_pubsub_GIL_publish(void *tsk_) {
  iodine_pubsub_task_s *task = tsk_;
  VALUE args[2];
  fio_cstr_s tmp = fiobj_obj2cstr(task->ch);
  args[0] = rb_str_new(tmp.data, tmp.len);
  tmp = fiobj_obj2cstr(task->msg);
  args[1] = rb_str_new(tmp.data, tmp.len);
  IodineCaller.call2(task->eng->handler, publish_id, 2, args);
  return NULL;
}

/** Should return 0 on success and -1 on failure. */
static int iodine_pubsub_on_publish(const pubsub_engine_s *eng, FIOBJ channel,
                                    FIOBJ msg) {
  if (iodine_engine(eng)->handler == Qnil) {
    return -1;
  }
  iodine_pubsub_task_s task = {
      .eng = iodine_engine(eng), .ch = channel, .msg = msg};
  IodineCaller.enterGVL(iodine_pubsub_GIL_publish, &task);
  return 0;
}
/**
 * facil.io will call this callback whenever starting, or restarting, the
 * reactor.
 *
 * but iodine engines should probably use the `before_fork` and `after_fork`
 * hooks.
 */
static void iodine_pubsub_on_startup(const pubsub_engine_s *eng) { (void)eng; }

/* *****************************************************************************
Ruby methods
***************************************************************************** */

/**
OVERRIDE this callback - it will be called by {Iodine} whenever the process
CLUSTER (not just this process) subscribes to a stream / channel.
*/
static VALUE iodine_pubsub_subscribe(VALUE self, VALUE to, VALUE match) {
  return Qnil;
#if 0
  iodine_pubsub_s *e = iodine_pubsub_CData(self);
  if (e->engine == &e->do_not_touch) {
    /* this is a Ruby engine, nothing to do. */
    return Qnil;
  }
  FIOBJ ch = fiobj_str_new(RSTRING_PTR(to), RSTRING_LEN(to));
  e->engine->subscribe(e->engine, ch, SYM2ID(match) == redis_id);
  fiobj_free(ch);
  return to;
#endif
  (void)self;
  (void)to;
  (void)match;
}

/**
OVERRIDE this callback - it will be called by {Iodine} whenever the whole
process CLUSTER (not just this process) unsubscribes from a stream / channel.
*/
static VALUE iodine_pubsub_unsubscribe(VALUE self, VALUE to, VALUE match) {
  return Qnil;
#if 0
  iodine_pubsub_s *e = iodine_pubsub_CData(self);
  if (e->engine == &e->do_not_touch) {
    /* this is a Ruby engine, nothing to do. */
    return Qnil;
  }
  FIOBJ ch = fiobj_str_new(RSTRING_PTR(to), RSTRING_LEN(to));
  e->engine->unsubscribe(e->engine, ch, SYM2ID(match) == redis_id);
  fiobj_free(ch);
  return to;
#endif
  (void)self;
  (void)to;
  (void)match;
}

/**
OVERRIDE this callback - it will be called by {Iodine} whenever the
{Iodine.publish} (or {Iodine::Connection#publish}) is called for this engine.

If this {Engine} is set as the default {Engine}, then any call to
{Iodine.publish} (or {Iodine::Connection#publish} will invoke this callback
(unless another {Engine} was specified).

NOTE: this callback is called per process event (not per cluster event) and the
{Engine} is responsible for message propagation.
*/
static VALUE iodine_pubsub_publish(VALUE self, VALUE to, VALUE message) {
  iodine_pubsub_s *e = iodine_pubsub_CData(self);
  if (e->engine == &e->do_not_touch) {
    /* this is a Ruby engine, nothing to do. */
    return Qnil;
  }
  FIOBJ ch, msg;
  ch = fiobj_str_new(RSTRING_PTR(to), RSTRING_LEN(to));
  msg = fiobj_str_new(RSTRING_PTR(message), RSTRING_LEN(message));
  e->engine->publish(e->engine, ch, msg);
  fiobj_free(ch);
  fiobj_free(msg);
  return self;
  (void)self;
  (void)to;
  (void)message;
}

/* *****************************************************************************
Ruby <=> C Data Type
***************************************************************************** */

/* a callback for the GC (marking active objects) */
static void iodine_pubsub_data_mark(void *c_) {
  iodine_pubsub_s *c = c_;
  if (c->handler != Qnil) {
    rb_gc_mark(c->handler);
  }
}
/* a callback for the GC (marking active objects) */
static void iodine_pubsub_data_free(void *c_) {
  iodine_pubsub_s *data = c_;
  if (data->dealloc) {
    data->dealloc(data->engine);
  }
  free(data);
}

static size_t iodine_pubsub_data_size(const void *c_) {
  return sizeof(iodine_pubsub_s);
  (void)c_;
}

const rb_data_type_t iodine_pubsub_data_type = {
    .wrap_struct_name = "IodinePubSubData",
    .function =
        {
            .dmark = iodine_pubsub_data_mark,
            .dfree = iodine_pubsub_data_free,
            .dsize = iodine_pubsub_data_size,
        },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/* Iodine::PubSub::Engine.allocate */
static VALUE iodine_pubsub_data_alloc_c(VALUE self) {
  iodine_pubsub_s *c = malloc(sizeof(*c));
  *c = (iodine_pubsub_s){
      .do_not_touch =
          {
              .subscribe = iodine_pubsub_on_subscribe,
              .unsubscribe = iodine_pubsub_on_unsubscribe,
              .publish = iodine_pubsub_on_publish,
              .on_startup = iodine_pubsub_on_startup,
          },
      .handler = Qnil,
      .engine = &c->do_not_touch,
  };
  return TypedData_Wrap_Struct(self, &iodine_pubsub_data_type, c);
}

/* *****************************************************************************
C engines
***************************************************************************** */

static VALUE iodine_pubsub_make_C_engine(const pubsub_engine_s *e) {
  VALUE engine = IodineCaller.call(EngineClass, rb_intern2("new", 3));
  if (engine == Qnil) {
    return Qnil;
  }
  iodine_pubsub_CData(engine)->engine = (pubsub_engine_s *)e;
  return engine;
}

/* *****************************************************************************
PubSub module methods
***************************************************************************** */

/** Sets the default {Iodine::PubSub::Engine} for pub/sub methods. */
static VALUE iodine_pubsub_default_set(VALUE self, VALUE engine) {
  if (engine == Qnil) {
    engine = rb_const_get(self, rb_intern2("CLUSTER", 7));
  }
  iodine_pubsub_s *e = iodine_pubsub_CData(engine);
  if (!e) {
    rb_raise(rb_eTypeError, "not a valid engine");
    return Qnil;
  }
  if (e->handler == Qnil) {
    e->handler = engine;
  }
  PUBSUB_DEFAULT_ENGINE = e->engine;
  rb_ivar_set(self, rb_intern2("default_engine", 14), engine);
  return engine;
}

/** Returns the default {Iodine::PubSub::Engine} for pub/sub methods. */
static VALUE iodine_pubsub_default_get(VALUE self) {
  VALUE def = rb_ivar_get(self, rb_intern2("default_engine", 14));
  if (def == Qnil) {
    def = rb_const_get(self, rb_intern2("CLUSTER", 7));
    iodine_pubsub_default_set(self, def);
  }
  return def;
}

/**
 * Attaches an {Iodine::PubSub::Engine} to the pub/sub system (more than a
 * single engine can be attached at the same time).
 *
 * After an engine was attached, it's callbacks will be called
 * ({Iodine::PubSub::Engine#subscribe} and {Iodine::PubSub::Engine#unsubscribe})
 * in response to Pub/Sub events.
 */
static VALUE iodine_pubsub_attach(VALUE self, VALUE engine) {
  iodine_pubsub_s *e = iodine_pubsub_CData(engine);
  if (!e) {
    rb_raise(rb_eTypeError, "not a valid engine");
    return Qnil;
  }
  if (e->handler == Qnil) {
    e->handler = engine;
  }
  IodineStore.add(engine);
  pubsub_engine_register(e->engine);
  return engine;
  (void)self;
}

/**
 * Removes an {Iodine::PubSub::Engine} from the pub/sub system.
 *
 * After an {Iodine::PubSub::Engine} was detached, Iodine will no longer call
 * the {Iodine::PubSub::Engine}'s callbacks ({Iodine::PubSub::Engine#subscribe}
 * and {Iodine::PubSub::Engine#unsubscribe})
 */
static VALUE iodine_pubsub_dettach(VALUE self, VALUE engine) {
  iodine_pubsub_s *e = iodine_pubsub_CData(engine);
  if (!e) {
    rb_raise(rb_eTypeError, "not a valid engine");
    return Qnil;
  }
  if (e->handler == Qnil) {
    e->handler = engine;
  }
  IodineStore.remove(engine);
  pubsub_engine_deregister(e->engine);
  return engine;
  (void)self;
}

/**
 * Forces {Iodine} to call the {Iodine::PubSub::Engine#subscribe} callback for
 * all existing subscriptions (i.e., when reconnecting to a Pub/Sub backend such
 * as Redis).
 */
static VALUE iodine_pubsub_reset(VALUE self, VALUE engine) {
  iodine_pubsub_s *e = iodine_pubsub_CData(engine);
  if (!e) {
    rb_raise(rb_eTypeError, "not a valid engine");
    return Qnil;
  }
  if (e->handler == Qnil) {
    e->handler = engine;
  }
  pubsub_engine_resubscribe(e->engine);
  return engine;
  (void)self;
}

/* *****************************************************************************
Redis Engine
***************************************************************************** */

/**
Initializes a new {Iodine::PubSub::Redis} engine.

    Iodine::PubSub::Redis.new(url, opt = {})

use:

    REDIS_URL = "redis://localhost:6379/"
    Iodine::PubSub::Redis.new(REDIS_URL, ping: 50) #pings every 50 seconds

To use Redis authentication, add the password to the URL. i.e.:

    REDIS_URL = "redis://redis:password@localhost:6379/"
    Iodine::PubSub::Redis.new(REDIS_URL, ping: 50) #pings every 50 seconds

The options hash accepts:

:ping:: the PING interval up to 255 seconds. Default: 0 (~5 minutes).
*/
static VALUE iodine_pubsub_redis_new(int argc, VALUE *argv, VALUE self) {
  if (!argc) {
    rb_raise(rb_eArgError, "Iodine::PubSub::Redis.new(address, opt={}) "
                           "requires at least 1 argument.");
  }
  VALUE url = argv[0];
  Check_Type(url, T_STRING);
  if (RSTRING_LEN(url) > 4096) {
    rb_raise(rb_eArgError, "Redis URL too long.");
  }
  FIOBJ port = FIOBJ_INVALID;
  FIOBJ address = FIOBJ_INVALID;
  FIOBJ auth = FIOBJ_INVALID;
  uint8_t ping = 0;

  iodine_pubsub_s *e = iodine_pubsub_CData(self);
  if (!e) {
    rb_raise(rb_eTypeError, "not a valid engine");
    return Qnil;
  }

  /* extract options */
  if (argc == 2) {
    Check_Type(argv[1], T_HASH);
    VALUE tmp = rb_hash_aref(argv[1], rb_id2sym(rb_intern2("ping", 4)));
    if (tmp != Qnil) {
      Check_Type(tmp, T_FIXNUM);
      if (NUM2SIZET(tmp) > 255) {
        rb_raise(rb_eArgError,
                 ":ping must be a non-negative integer under 255 seconds.");
      }
      ping = (uint8_t)NUM2SIZET(tmp);
    }
  }

  /* parse URL assume redis://redis:password@localhost:6379 */
  {
    size_t l = RSTRING_LEN(url);
    char *str = RSTRING_PTR(url);
    char *pointers[5];
    char *end = str + l;
    uint8_t flag = 1;
    uint8_t counter = 0;
    for (size_t i = 0; i < l; i++) {
      if (counter > 4)
        goto finish;
      if (str[i] == ':' && str[i + 1] == '/' && str[i + 2] == '/') {
        pointers[counter++] = str + i + 3;
        i = i + 2;
        flag = 0;
        continue;
      }
      if (str[i] == '@' && counter == 1 - flag) {
        rb_raise(rb_eArgError, "malformed URL");
      }
      if (str[i] == ':' || str[i] == '@') {
        pointers[counter++] = str + i + 1;
        continue;
      }
      if (str[i] == '/') {
        end = str + i;
        break;
      }
    }
    if (flag) {
      if (counter > 3) {
        rb_raise(rb_eArgError, "malformed URL");
      }
      /* move pointers one step forward and set 0 to str... */
      char *pointers_2[5];
      for (size_t i = 0; i < counter; ++i) {
        pointers_2[i + 1] = pointers[i];
      }
      pointers_2[0] = str;
      ++counter;
      for (size_t i = 0; i < counter; ++i) {
        pointers[i] = pointers_2[i];
      }
    }
    /* review results */
    switch (counter) {
    case 1:
      /* redis://localhost */
      if (pointers[0] == end) {
        goto finish;
      }
      address = fiobj_str_new(pointers[0], end - pointers[0]);
      break;
    case 2:
      /* redis://localhost:6379 */
      if (pointers[1] - pointers[0] - 1 == 0) {
        goto finish;
      }
      address = fiobj_str_new(pointers[0], pointers[1] - pointers[0] - 1);
      if (pointers[1] != end) {
        port = fiobj_str_new(pointers[1], end - pointers[1]);
      }
      break;
    case 3:
      /* redis://redis:password@localhost */
      if (pointers[2] - pointers[1] - 1 == 0 || end - pointers[2] == 0) {
        goto finish;
      }
      address = fiobj_str_new(pointers[2], end - pointers[2]);
      auth = fiobj_str_new(pointers[1], pointers[2] - pointers[1] - 1);
      break;
    case 4:
      /* redis://redis:password@localhost:6379 */
      if (pointers[2] - pointers[1] - 1 == 0 ||
          pointers[3] - pointers[2] - 1 == 0 || end - pointers[3] == 0) {
        goto finish;
      }
      port = fiobj_str_new(pointers[3], end - pointers[3]);
      address = fiobj_str_new(pointers[2], pointers[3] - pointers[2] - 1);
      auth = fiobj_str_new(pointers[1], pointers[2] - pointers[1] - 1);
      break;
    default:
      goto finish;
    }
  }
  fprintf(
      stderr,
      "INFO: Initializing Redis engine for address: %s - port: %s -  auth %s\n",
      fiobj_obj2cstr(address).data, fiobj_obj2cstr(port).data,
      fiobj_obj2cstr(auth).data);
  /* create engine */
  e->engine = redis_engine_create(
          .address = fiobj_obj2cstr(address)
          .data,
          .port = (port == FIOBJ_INVALID ? "6379" : fiobj_obj2cstr(port).data),
          .ping_interval = ping,
          .auth = (auth == FIOBJ_INVALID ? NULL : fiobj_obj2cstr(auth).data),
          .auth_len = (auth == FIOBJ_INVALID ? 0 : fiobj_obj2cstr(auth).len));
  if (!e->engine) {
    e->engine = &e->do_not_touch;
  } else {
    e->dealloc = redis_engine_destroy;
  }

finish:
  fiobj_free(port);
  fiobj_free(address);
  fiobj_free(auth);
  if (e->engine == &e->do_not_touch) {
    rb_raise(rb_eArgError,
             "Error initializing the Redis engine - malformed URL?");
  }
  return self;
  (void)self;
  (void)argc;
  (void)argv;
}

/** A callback for Redis commands. */
static void iodine_pubsub_redis_callback(pubsub_engine_s *e, FIOBJ response,
                                         void *udata) {
  VALUE block = (VALUE)udata;
  if (block == Qnil) {
    return;
  }
  VALUE rb = Qnil;
  if (!FIOBJ_IS_NULL(response)) {
    rb = IodineStore.add(fiobj2rb_deep(response, 0));
  }
  IodineCaller.call2(block, call_id, 1, &rb);
  IodineStore.remove(rb);
  IodineStore.remove(block);
  (void)e;
}

// clang-format off
/**
Sends a Redis command. Accepts an optional block that will recieve the response.

i.e.:

    REDIS_URL = "redis://redis:password@localhost:6379/"
    redis = Iodine::PubSub::Redis.new(REDIS_URL, ping: 50) #pings every 50 seconds
    Iodine::PubSub.default = redis
    redis.cmd("KEYS", "*") {|result| p result
}


*/
static VALUE iodine_pubsub_redis_cmd(int argc, VALUE *argv, VALUE self) {
  // clang-format on
  if (argc <= 0) {
    rb_raise(rb_eArgError, "Iodine::PubSub::Redis#cmd(command, ...) is missing "
                           "the required command argument.");
  }
  iodine_pubsub_s *e = iodine_pubsub_CData(self);
  if (!e || !e->engine || e->engine == &e->do_not_touch) {
    rb_raise(rb_eTypeError,
             "Iodine::PubSub::Redis internal error - obsolete object?");
  }
  VALUE block = Qnil;
  if (rb_block_given_p()) {
    block = IodineStore.add(rb_block_proc());
  }
  FIOBJ data = fiobj_ary_new2((size_t)argc);
  for (int i = 0; i < argc; ++i) {
    switch (TYPE(argv[i])) {
    case T_SYMBOL:
      argv[i] = rb_sym2str(argv[i]);
    /* overflow */
    case T_STRING:
      fiobj_ary_push(data,
                     fiobj_str_new(RSTRING_PTR(argv[i]), RSTRING_LEN(argv[i])));
      break;
    case T_FIXNUM:
      fiobj_ary_push(data, fiobj_num_new(NUM2SSIZET(argv[i])));
      break;
    case T_FLOAT:
      fiobj_ary_push(data, fiobj_float_new(rb_float_value(argv[i])));
      break;
    case T_NIL:
      fiobj_ary_push(data, fiobj_null());
      break;
    case T_TRUE:
      fiobj_ary_push(data, fiobj_true());
      break;
    case T_FALSE:
      fiobj_ary_push(data, fiobj_false());
      break;
    default:
      goto wrong_type;
    }
  }
  FIOBJ cmd = fiobj_ary_shift(data);
  if (redis_engine_send(e->engine, cmd, data, iodine_pubsub_redis_callback,
                        (void *)block)) {
    iodine_pubsub_redis_callback(e->engine, fiobj_null(), (void *)block);
  }
  fiobj_free(cmd);
  fiobj_free(data);
  return self;

wrong_type:
  fiobj_free(data);
  rb_raise(rb_eArgError,
           "only String, Number (with limits), Symbol, true, false and nil "
           "arguments can be used.");
}

/* *****************************************************************************
Module initialization
***************************************************************************** */

/** Initializes the Connection Ruby class. */
void iodine_pubsub_init(void) {
  subscribe_id = rb_intern2("subscribe", 9);
  unsubscribe_id = rb_intern2("unsubscribe", 11);
  publish_id = rb_intern2("publish", 7);
  default_id = rb_intern2("default_engine", 14);
  redis_id = rb_intern2("redis", 5);
  call_id = rb_intern2("call", 4);

  /* Define the PubSub module and it's methods */

  VALUE PubSubModule = rb_define_module_under(IodineModule, "PubSub");
  rb_define_module_function(PubSubModule, "default=", iodine_pubsub_default_set,
                            1);
  rb_define_module_function(PubSubModule, "default", iodine_pubsub_default_get,
                            0);
  rb_define_module_function(PubSubModule, "attach", iodine_pubsub_attach, 1);
  rb_define_module_function(PubSubModule, "dettach", iodine_pubsub_dettach, 1);
  rb_define_module_function(PubSubModule, "reset", iodine_pubsub_reset, 1);

  /* Define the Engine class and it's methods */

  /**
  The {Iodine::PubSub::Engine} class is the parent for all engines to inherit
  from.

  Engines should inherit this class and override the `subscribe`, `unsubscribe`
  and `publish` callbacks (which shall be called by {Iodine}).

  After creation, Engines should attach themselves to Iodine using
  {Iodine::PubSub.attach} or their callbacks will never get called.

  Engines can also set themselves to be the default engine using
  {Iodine::PubSub.default=}.
  */
  EngineClass = rb_define_class_under(PubSubModule, "Engine", rb_cObject);
  rb_define_alloc_func(EngineClass, iodine_pubsub_data_alloc_c);
  rb_define_method(EngineClass, "subscribe", iodine_pubsub_subscribe, 2);
  rb_define_method(EngineClass, "unsubscribe", iodine_pubsub_unsubscribe, 2);
  rb_define_method(EngineClass, "publish", iodine_pubsub_publish, 2);

  /* Define the CLUSTER and PROCESS engines */

  /* CLUSTER publishes data to all the subscribers in the process cluster. */
  rb_define_const(PubSubModule, "CLUSTER",
                  iodine_pubsub_make_C_engine(PUBSUB_CLUSTER_ENGINE));
  /* PROCESS publishes data to all the subscribers in a single process. */
  rb_define_const(PubSubModule, "PROCESS",
                  iodine_pubsub_make_C_engine(PUBSUB_PROCESS_ENGINE));

  VALUE RedisClass = rb_define_class_under(PubSubModule, "Redis", EngineClass);
  rb_define_method(RedisClass, "initialize", iodine_pubsub_redis_new, -1);
  rb_define_method(RedisClass, "cmd", iodine_pubsub_redis_cmd, -1);
}
