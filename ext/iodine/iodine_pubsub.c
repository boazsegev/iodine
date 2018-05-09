#include "iodine_pubsub.h"
#include "pubsub.h"

/* *****************************************************************************
static consts
***************************************************************************** */

static ID subscribe_id;
static ID unsubscribe_id;
static ID publish_id;
static ID default_id;

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
  (void)self;
  (void)to;
  (void)match;
}

/**
OVERRIDE this callback - it will be called by {Iodine} whenever the
{Iodine.publish} (or {Iosine::Connection#publish}) is called for this engine (or
if this engine is set as the default engine). This is per process (not per
cluster) and the Engine is responsible for message propagation.
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
  return Qnil;
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

/** Removed an {Iodine::PubSub::Engine} from the pub/sub system. */
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
Module initialization
***************************************************************************** */

/** Initializes the Connection Ruby class. */
void iodine_pubsub_init(void) {
  subscribe_id = rb_intern2("subscribe", 9);
  unsubscribe_id = rb_intern2("unsubscribe", 11);
  publish_id = rb_intern2("publish", 7);
  default_id = rb_intern2("default_engine", 14);

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

  EngineClass = rb_define_class_under(PubSubModule, "Engine", rb_cObject);
  rb_define_alloc_func(EngineClass, iodine_pubsub_data_alloc_c);
  rb_define_method(EngineClass, "subscribe", iodine_pubsub_subscribe, 2);
  rb_define_method(EngineClass, "unsubscribe", iodine_pubsub_unsubscribe, 2);
  rb_define_method(EngineClass, "publish", iodine_pubsub_publish, 2);

  /* Define the CLUSTER and PROCESS engines */

  /* CLUSTER publishes data to all the subscribers in the process cluster. */
  rb_define_const(PubSubModule, "CLUSTER",
                  iodine_pubsub_make_C_engine(PUBSUB_CLUSTER_ENGINE));
  /* CLUSTER publishes data to all the subscribers in a single process. */
  rb_define_const(PubSubModule, "PROCESS",
                  iodine_pubsub_make_C_engine(PUBSUB_PROCESS_ENGINE));
}
