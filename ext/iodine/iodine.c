/* core include */
#include "iodine.h"

/* *****************************************************************************
Starting / Stooping the IO Reactor
***************************************************************************** */

static fio_srv_async_s IODINE_THREAD_POOL;

static void iodine_stop___(void *ignr_) {
  fio_srv_stop();
  (void)ignr_;
}

static void *iodine___run(void *ignr_) {
  fio_srv_async_init(&IODINE_THREAD_POOL, (uint32_t)fio_cli_get_i("-t"));
  VALUE ver = rb_const_get(iodine_rb_IODINE, rb_intern2("VERSION", 7));
  FIO_LOG_INFO("\n\tStarting the iodine server."
               "\n\tVersion: %s"
               "\n\tEngine: " FIO_POLL_ENGINE_STR "\n\tWorkers: %d\t(%s)"
               "\n\tThreads: 1+%d\t(per worker)"
               "\n\tPress ^C to exit.",
               (ver == Qnil ? "unknown" : RSTRING_PTR(ver)),
               fio_srv_workers(fio_cli_get_i("-w")),
               (fio_srv_workers(fio_cli_get_i("-w")) ? "cluster mode"
                                                     : "single process"),
               (int)IODINE_THREAD_POOL.count);

  fio_srv_start((int)fio_cli_get_i("-w"));
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
  fio_srv_stop();
  return klass;
}

/** Return `true` if reactor is running */
static VALUE iodine_is_running(VALUE klass) {
  return fio_srv_is_running() ? Qtrue : Qfalse;
}

/** Return `true` if this is the master process. */
static VALUE iodine_is_master(VALUE klass) {
  return fio_srv_is_master() ? Qtrue : Qfalse;
}

/** Return `true` if this is a worker process. */
static VALUE iodine_is_worker(VALUE klass) {
  return fio_srv_is_worker() ? Qtrue : Qfalse;
}

/* *****************************************************************************
Workers
***************************************************************************** */

/** Returns the number of process workers that the reactor (will) use. */
static VALUE iodine_workers(VALUE klass) {
  return LL2NUM(fio_cli_get_i("-w"));
  (void)klass;
}

/**
 * Sets the number of process workers that the reactor will use.
 *
 * Settable only in the root / master process.
 */
static VALUE iodine_workers_set(VALUE klass, VALUE workers) {
  if (workers != Qnil && fio_srv_is_master()) {
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
  return LL2NUM(fio_cli_get_i("-w"));
  (void)klass;
}

/**
 * Sets the number of threads that the reactor will use.
 *
 * Settable only in the root / master process.
 */
static VALUE iodine_threads_set(VALUE klass, VALUE threads) {
  if (threads != Qnil && fio_srv_is_master()) {
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
Initialize module
***************************************************************************** */

void Init_iodine_ext(void) {
  fio_state_callback_force(FIO_CALL_ON_INITIALIZE);

  IodineUTF8Encoding = rb_enc_find("UTF-8");
  IodineBinaryEncoding = rb_enc_find("binary");

  /** The Iodine module is where it all happens. */
  iodine_rb_IODINE = rb_define_module("Iodine");
  /** The PubSub module contains Pub/Sub related classes / data. */
  iodine_rb_IODINE_PUBSUB = rb_define_module_under(iodine_rb_IODINE, "PubSub");
  /** The Iodine::Base module is for internal concerns. */
  iodine_rb_IODINE_BASE =
      rb_define_class_under(iodine_rb_IODINE, "Base", rb_cObject);
  /** Initialize `STORE` and object reference counting. */
  iodine_setup_value_reference_counter(iodine_rb_IODINE_BASE);

  /* Setup some constants and don't move them around in memory */
  STORE.hold(iodine_rb_IODINE);
  STORE.hold(iodine_rb_IODINE_BASE);
  STORE.hold(iodine_rb_IODINE_PUBSUB);

  IODINE_CONST_ID_STORE(IODINE_CALL_ID, "call");
  IODINE_CONST_ID_STORE(IODINE_NEW_ID, "new");
  IODINE_CONST_ID_STORE(IODINE_ON_ATTACH_ID, "on_attach");
  IODINE_CONST_ID_STORE(IODINE_ON_AUTHENTICATE_SSE_ID, "on_authenticate_sse");
  IODINE_CONST_ID_STORE(IODINE_ON_AUTHENTICATE_WEBSOCKET_ID,
                        "on_authenticate_websocket");
  IODINE_CONST_ID_STORE(IODINE_ON_CLOSE_ID, "on_close");
  IODINE_CONST_ID_STORE(IODINE_ON_DATA_ID, "on_data");
  IODINE_CONST_ID_STORE(IODINE_ON_EVENTSOURCE_ID, "on_eventsource");
  IODINE_CONST_ID_STORE(IODINE_ON_EVENTSOURCE_RECONNECT_ID,
                        "on_eventsource_reconnect");
  IODINE_CONST_ID_STORE(IODINE_ON_FINISH_ID, "on_finish");
  IODINE_CONST_ID_STORE(IODINE_ON_HTTP_ID, "on_http");
  IODINE_CONST_ID_STORE(IODINE_ON_MESSAGE_ID, "on_message");
  IODINE_CONST_ID_STORE(IODINE_ON_OPEN_ID, "on_open");
  IODINE_CONST_ID_STORE(IODINE_ON_READY_ID, "on_ready");
  IODINE_CONST_ID_STORE(IODINE_ON_SHUTDOWN_ID, "on_shutdown");
  IODINE_CONST_ID_STORE(IODINE_ON_TIMEOUT_ID, "on_timeout");
  IODINE_CONST_ID_STORE(IODINE_PRE_HTTP_BODY_ID, "pre_http_body");

  rb_define_singleton_method(iodine_rb_IODINE, "start", iodine_start, 0);
  rb_define_singleton_method(iodine_rb_IODINE, "stop", iodine_stop, 0);

  rb_define_singleton_method(iodine_rb_IODINE,
                             "running?",
                             iodine_is_running,
                             0);
  rb_define_singleton_method(iodine_rb_IODINE, "master?", iodine_is_master, 0);
  rb_define_singleton_method(iodine_rb_IODINE, "worker?", iodine_is_worker, 0);

  rb_define_singleton_method(iodine_rb_IODINE, "workers", iodine_workers, 0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "workers=",
                             iodine_workers_set,
                             1);

  rb_define_singleton_method(iodine_rb_IODINE, "threads", iodine_threads, 0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "threads=",
                             iodine_threads_set,
                             1);

  rb_define_singleton_method(iodine_rb_IODINE,
                             "verbosity",
                             iodine_verbosity,
                             0);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "verbosity=",
                             iodine_verbosity_set,
                             1);
  Init_iodine_cli();
  Init_iodine_musta();
  Init_iodine_utils();
  Init_iodine_json();
  iodine_pubsub_msg_init();
  Init_iodine_connection();
  Init_iodine_defer();
}
