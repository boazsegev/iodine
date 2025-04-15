#ifndef H___IODINE_CORE___H
#define H___IODINE_CORE___H
#include "iodine.h"

/* *****************************************************************************
Starting / Stooping the IO Reactor
***************************************************************************** */

static void iodine_connection_cache_common_strings(void);
static void *iodine___start(void *ignr_) {
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
  rb_thread_call_without_gvl(iodine___start, NULL, NULL, NULL);
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
  rb_check_type(num, RUBY_T_FIXNUM);
  FIO_LOG_LEVEL_SET(RB_FIX2INT(num));
  return num;
}

/* *****************************************************************************
Secrets
***************************************************************************** */

/** Returns the current verbosity (logging) level.*/
static VALUE iodine_secret(VALUE klass) {
  fio_u512 s = fio_secret();
  return rb_usascii_str_new((const char *)s.u8, sizeof(s));
  (void)klass;
}

/** Sets the current verbosity (logging) level. */
static VALUE iodine_secret_set(VALUE klass, VALUE key) {
  rb_check_type(key, RUBY_T_STRING);
  fio_secret_set(RSTRING_PTR(key), RSTRING_LEN(key), 0);
  return iodine_secret(klass);
}

/* *****************************************************************************
Shutdown Timeouts
***************************************************************************** */

/** Returns the current verbosity (logging) level.*/
static VALUE iodine_shutdown_timeout(VALUE klass) {
  return RB_SIZE2NUM(fio_io_shutdown_timsout());
  (void)klass;
}

/** Sets the current verbosity (logging) level. */
static VALUE iodine_shutdown_timeout_set(VALUE klass, VALUE num) {
  rb_check_type(num, RUBY_T_FIXNUM);
  if (RB_NUM2SIZE(num) > (5U * 60U * 1000U))
    rb_raise(rb_eRangeError, "shutdown timeout out of range");
  fio_io_shutdown_timsout_set(RB_NUM2SIZE(num));
  return num;
}

#endif /* H___IODINE_CORE___H */
