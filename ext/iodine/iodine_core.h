#ifndef H___IODINE_CORE___H
#define H___IODINE_CORE___H
#include "iodine.h"

/* *****************************************************************************
Starting / Stooping the IO Reactor
***************************************************************************** */

static void iodine_stop___(void *ignr_) {
  fio_io_stop();
  (void)ignr_;
}

static void iodine_connection_cache_common_strings(void);
static void *iodine___run(void *ignr_) {
  VALUE ver = rb_const_get(iodine_rb_IODINE, rb_intern2("VERSION", 7));
  unsigned threads = (unsigned)fio_io_workers((int)fio_cli_get_i("-t"));
  unsigned workers = (unsigned)fio_io_workers((int)fio_cli_get_i("-w"));
  fio_io_async_attach(&IODINE_THREAD_POOL, (uint32_t)threads);

  iodine_env_set_const_val(IODINE_CONNECTION_ENV_TEMPLATE,
                           FIO_STR_INFO1((char *)"rack.multithread"),
                           (fio_cli_get_i("-t") ? Qtrue : Qfalse));
  iodine_env_set_const_val(IODINE_CONNECTION_ENV_TEMPLATE,
                           FIO_STR_INFO1((char *)"rack.multiprocess"),
                           (fio_cli_get_i("-w") ? Qtrue : Qfalse));

  FIO_LOG_INFO("\n\tStarting the iodine server."
               "\n\tVersion: %s"
               "\n\tEngine: " FIO_POLL_ENGINE_STR "\n\tWorkers: %d\t(%s)"
               "\n\tThreads: 1+%d\t(per worker)"
               "\n\tPress ^C to exit.",
               (ver == Qnil ? "unknown" : RSTRING_PTR(ver)),
               workers,
               (workers ? "cluster mode" : "single process"),
               (int)IODINE_THREAD_POOL.count);

  fio_io_start((int)fio_cli_get_i("-w"));
  return ignr_;
}

/** Starts the Iodine IO reactor. */
static VALUE iodine_start(VALUE self) { // clang-format on
  rb_thread_call_without_gvl(iodine___run, NULL, iodine_stop___, NULL);
  return self;
}

/**
 * Stops the current process' IO reactor.
 *
 * If this is a worker process, the process will exit and if Iodine is running a
 * new worker will be spawned.
 */
static VALUE iodine_stop(VALUE klass) {
  fio_io_stop();
  return klass;
}

/** Return `true` if reactor is running */
static VALUE iodine_is_running(VALUE klass) {
  return fio_io_is_running() ? Qtrue : Qfalse;
}

/** Return `true` if this is the master process. */
static VALUE iodine_is_master(VALUE klass) {
  return fio_io_is_master() ? Qtrue : Qfalse;
}

/** Return `true` if this is a worker process. */
static VALUE iodine_is_worker(VALUE klass) {
  return fio_io_is_worker() ? Qtrue : Qfalse;
}

/* *****************************************************************************
Workers
***************************************************************************** */

/** Returns the number of process workers that the reactor (will) use. */
static VALUE iodine_workers(VALUE klass) {
  if (!fio_cli_get("-w"))
    return Qnil;
  unsigned long tmp = fio_io_workers((uint16_t)fio_cli_get_i("-w"));
  return LL2NUM(tmp);
  (void)klass;
}

/**
 * Sets the number of process workers that the reactor will use.
 *
 * Settable only in the root / master process.
 */
static VALUE iodine_workers_set(VALUE klass, VALUE workers) {
  if (workers != Qnil && fio_io_is_master()) {
    if (TYPE(workers) != T_FIXNUM) {
      rb_raise(rb_eTypeError, "workers must be a number.");
      return Qnil;
    }
    fio_cli_set_i("-w", NUM2LL(workers));
  } else {
    FIO_LOG_ERROR(
        "cannot set workers except as a numeral value in the master process");
  }
  workers = LL2NUM(fio_cli_get_i("-w"));
  return workers;
}

/* *****************************************************************************
Threads
***************************************************************************** */

/** Returns the number of threads that the reactor (will) use. */
static VALUE iodine_threads(VALUE klass) {
  if (!fio_cli_get("-t"))
    return Qnil;
  unsigned long tmp = fio_io_workers((uint16_t)fio_cli_get_i("-t"));
  return LL2NUM(tmp);
  (void)klass;
}

/**
 * Sets the number of threads that the reactor will use.
 *
 * Settable only in the root / master process.
 */
static VALUE iodine_threads_set(VALUE klass, VALUE threads) {
  if (threads != Qnil && fio_io_is_master()) {
    if (TYPE(threads) != T_FIXNUM) {
      rb_raise(rb_eTypeError, "threads must be a number.");
      return Qnil;
    }
    fio_cli_set_i("-t", NUM2LL(threads));
  } else {
    FIO_LOG_ERROR(
        "cannot set threads except as a numeral value in the master process");
  }
  threads = LL2NUM(fio_cli_get_i("-t"));
  return threads;
}

/* *****************************************************************************
Verbosity
***************************************************************************** */

/** Returns the current verbosity (logging) level.*/
static VALUE iodine_verbosity(VALUE klass) {
  return RB_INT2FIX(((long)FIO_LOG_LEVEL_GET()));
  (void)klass;
}

/** Sets the current verbosity (logging) level. */
static VALUE iodine_verbosity_set(VALUE klass, VALUE num) {
  FIO_LOG_LEVEL_SET(RB_FIX2INT(num));
  return num;
}

/* *****************************************************************************
FIOBJ => Ruby translation later
***************************************************************************** */
typedef struct iodine_fiobj2ruby_task_s {
  VALUE out;
} iodine_fiobj2ruby_task_s;

static VALUE iodine_fiobj2ruby(FIOBJ o);

static int iodine_fiobj2ruby_array_task(fiobj_array_each_s *e) {
  rb_ary_push((VALUE)e->udata, iodine_fiobj2ruby(e->value));
  return 0;
}
static int iodine_fiobj2ruby_hash_task(fiobj_hash_each_s *e) {
  rb_hash_aset((VALUE)e->udata,
               iodine_fiobj2ruby(e->key),
               iodine_fiobj2ruby(e->value));
  return 0;
}

/** Converts FIOBJ to VALUE. Does NOT place VALUE in STORE automatically. */
static VALUE iodine_fiobj2ruby(FIOBJ o) {
  VALUE r;
  switch (FIOBJ_TYPE(o)) {
  case FIOBJ_T_TRUE: return Qtrue;
  case FIOBJ_T_FALSE: return Qfalse;
  case FIOBJ_T_NUMBER: return RB_LL2NUM(fiobj_num2i(o));
  case FIOBJ_T_FLOAT: return rb_float_new(fiobj_float2f(o));
  case FIOBJ_T_STRING: return rb_str_new(fiobj_str_ptr(o), fiobj_str_len(o));
  case FIOBJ_T_ARRAY:
    r = rb_ary_new_capa(fiobj_array_count(o));
    STORE.hold(r);
    fiobj_array_each(o, iodine_fiobj2ruby_array_task, (void *)r, 0);
    STORE.release(r);
    return r;
  case FIOBJ_T_HASH:
    r = rb_hash_new();
    STORE.hold(r);
    fiobj_hash_each(o, iodine_fiobj2ruby_hash_task, (void *)r, 0);
    STORE.release(r);
    return r;
  // case FIOBJ_T_NULL: /* fall through */
  // case FIOBJ_T_INVALID: /* fall through */
  default: return Qnil;
  }
}

#endif /* H___IODINE_CORE___H */
