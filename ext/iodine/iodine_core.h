#ifndef H___IODINE_CORE___H
#define H___IODINE_CORE___H
#include "iodine.h"

/* *****************************************************************************
Iodine Core - IO Reactor Control

This module provides the core functionality for controlling the Iodine IO
reactor, including:

- Starting and stopping the reactor event loop
- Querying reactor state (running, master/worker process)
- Configuring worker processes and threads
- Managing verbosity/logging levels
- Server secrets for cryptographic operations
- Graceful shutdown timeout configuration

The reactor uses a multi-process/multi-thread architecture where:
- Master process manages worker processes
- Each worker process runs its own event loop with multiple threads
- Thread pool handles async tasks within each worker

Ruby API Methods (defined on Iodine module):
- Iodine.start          - Start the IO reactor
- Iodine.stop           - Stop the current process' reactor
- Iodine.running?       - Check if reactor is running
- Iodine.master?        - Check if this is the master process
- Iodine.worker?        - Check if this is a worker process
- Iodine.workers / =    - Get/set number of worker processes
- Iodine.threads / =    - Get/set number of threads per worker
- Iodine.verbosity / =  - Get/set logging verbosity level
- Iodine.secret / =     - Get/set server secret key
- Iodine.shutdown_timeout / = - Get/set graceful shutdown timeout
***************************************************************************** */

/* *****************************************************************************
Starting / Stopping the IO Reactor
***************************************************************************** */

static void iodine_connection_cache_common_strings(void);

/** C-only data passed from iodine_start() (GVL held) to iodine___start()
 *  (GVL released) so that iodine___start() never touches Ruby API. */
typedef struct {
  const char *version; /* C string copy of Iodine::VERSION */
} iodine___start_args_s;

static void *iodine___start(void *arg_) {
  iodine___start_args_s *args = (iodine___start_args_s *)arg_;
  unsigned threads = (unsigned)fio_io_workers((int)fio_cli_get_i("-t"));
  unsigned workers = (unsigned)fio_io_workers((int)fio_cli_get_i("-w"));
  fio_io_async_attach(&IODINE_THREAD_POOL, (uint32_t)threads);

  FIO_LOG_INFO("\n\tStarting the iodine server."
               "\n\tVersion: %s"
               "\n\tEngine: " FIO_POLL_ENGINE_STR "\n\tWorkers: %d\t(%s)"
               "\n\tThreads: 1+%d\t(per worker)"
               "\n\tPress ^C to exit.",
               args->version,
               workers,
               (workers ? "cluster mode" : "single process"),
               (int)IODINE_THREAD_POOL.count);

  fio_io_start((int)fio_cli_get_i("-w"));
  return NULL;
}

/**
 * Starts the Iodine IO reactor.
 *
 * This is the main entry point that begins the event loop. It:
 * - Attaches the async thread pool
 * - Sets rack.multithread/rack.multiprocess environment values
 * - Logs startup information (version, workers, threads)
 * - Starts the IO event loop (blocking until stopped)
 *
 * @param self The Iodine module (VALUE)
 * @return self
 *
 * Ruby: Iodine.start
 */
static VALUE iodine_start(VALUE self) { // clang-format on
  /* All Ruby API calls MUST happen here (GVL is held).
   * iodine___start() runs WITHOUT the GVL and must only use pure C. */
  iodine___start_args_s start_args = {.version = "unknown"};
  VALUE ver = rb_const_get(iodine_rb_IODINE, rb_intern2("VERSION", 7));
  if (ver != Qnil)
    start_args.version = RSTRING_PTR(ver);

  /* Set rack.multithread / rack.multiprocess env template values (Ruby API) */
  {
    VALUE keeper;
    iodine_env_set_const_val(IODINE_CONNECTION_ENV_TEMPLATE,
                             FIO_STR_INFO1((char *)"rack.multithread"),
                             (fio_cli_get_i("-t") ? Qtrue : Qfalse),
                             &keeper);
    iodine_env_set_const_val(IODINE_CONNECTION_ENV_TEMPLATE,
                             FIO_STR_INFO1((char *)"rack.multiprocess"),
                             (fio_cli_get_i("-w") ? Qtrue : Qfalse),
                             &keeper);
  }

  rb_thread_call_without_gvl(iodine___start, &start_args, NULL, NULL);
  return self;
}

/**
 * Stops the current process' IO reactor.
 *
 * If this is a worker process, the process will exit and if Iodine is running
 * in cluster mode, a new worker will be spawned by the master process.
 *
 * @param klass The Iodine module (VALUE)
 * @return klass
 *
 * Ruby: Iodine.stop
 */
static VALUE iodine_stop(VALUE klass) {
  fio_io_stop();
  return klass;
}

/**
 * Returns `true` if the IO reactor is currently running.
 *
 * @param klass The Iodine module (VALUE)
 * @return Qtrue if running, Qfalse otherwise
 *
 * Ruby: Iodine.running?
 */
static VALUE iodine_is_running(VALUE klass) {
  return fio_io_is_running() ? Qtrue : Qfalse;
}

/**
 * Returns `true` if this is the master (root) process.
 *
 * In cluster mode, the master process spawns and monitors worker processes.
 * In single-process mode, the process is both master and worker.
 *
 * @param klass The Iodine module (VALUE)
 * @return Qtrue if master process, Qfalse otherwise
 *
 * Ruby: Iodine.master?
 */
static VALUE iodine_is_master(VALUE klass) {
  return fio_io_is_master() ? Qtrue : Qfalse;
}

/**
 * Returns `true` if this is a worker process.
 *
 * Worker processes handle actual client connections and requests.
 * In single-process mode, the process is both master and worker.
 *
 * @param klass The Iodine module (VALUE)
 * @return Qtrue if worker process, Qfalse otherwise
 *
 * Ruby: Iodine.worker?
 */
static VALUE iodine_is_worker(VALUE klass) {
  return fio_io_is_worker() ? Qtrue : Qfalse;
}

/* *****************************************************************************
Workers - Process Pool Configuration
***************************************************************************** */

/**
 * Returns the number of worker processes that the reactor will use.
 *
 * Returns nil if workers haven't been configured yet.
 * A value of 0 means single-process mode (no forking).
 * A value > 0 enables cluster mode with that many worker processes.
 *
 * @param klass The Iodine module (VALUE)
 * @return Number of workers as Fixnum, or Qnil if not configured
 *
 * Ruby: Iodine.workers
 */
static VALUE iodine_workers(VALUE klass) {
  unsigned long tmp = fio_io_workers((uint16_t)fio_cli_get_i("-w"));
  return LL2NUM(tmp);
  (void)klass;
}

/**
 * Sets the number of worker processes that the reactor will use.
 *
 * Can only be set in the master process before starting the reactor.
 * Set to 0 for single-process mode, or > 0 for cluster mode.
 *
 * @param klass The Iodine module (VALUE)
 * @param workers Number of worker processes (Fixnum)
 * @return The configured number of workers
 *
 * @note Raises TypeError if workers is not a Fixnum.
 * @note Logs error if called from a worker process.
 *
 * Ruby: Iodine.workers = n
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
Threads - Thread Pool Configuration
***************************************************************************** */

/**
 * Returns the number of threads per worker that the reactor will use.
 *
 * Returns nil if threads haven't been configured yet.
 * Each worker process runs this many threads in its async thread pool.
 *
 * @param klass The Iodine module (VALUE)
 * @return Number of threads as Fixnum, or Qnil if not configured
 *
 * Ruby: Iodine.threads
 */
static VALUE iodine_threads(VALUE klass) {
  unsigned long tmp = fio_io_workers((uint16_t)fio_cli_get_i("-t"));
  return LL2NUM(tmp);
  (void)klass;
}

/**
 * Sets the number of threads per worker that the reactor will use.
 *
 * Can only be set in the master process before starting the reactor.
 * Each worker process will run this many threads in its async pool.
 *
 * @param klass The Iodine module (VALUE)
 * @param threads Number of threads per worker (Fixnum)
 * @return The configured number of threads
 *
 * @note Raises TypeError if threads is not a Fixnum.
 * @note Logs error if called from a worker process.
 *
 * Ruby: Iodine.threads = n
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
Verbosity - Logging Level Configuration
***************************************************************************** */

/**
 * Returns the current verbosity (logging) level.
 *
 * Log levels (from facil.io):
 * - 0: FIO_LOG_LEVEL_NONE    - No logging
 * - 1: FIO_LOG_LEVEL_FATAL   - Fatal errors only
 * - 2: FIO_LOG_LEVEL_ERROR   - Errors and above
 * - 3: FIO_LOG_LEVEL_WARNING - Warnings and above
 * - 4: FIO_LOG_LEVEL_INFO    - Info and above (default)
 * - 5: FIO_LOG_LEVEL_DEBUG   - Debug and above
 *
 * @param klass The Iodine module (VALUE)
 * @return Current log level as Fixnum
 *
 * Ruby: Iodine.verbosity
 */
static VALUE iodine_verbosity(VALUE klass) {
  return RB_INT2FIX(((long)FIO_LOG_LEVEL_GET()));
  (void)klass;
}

/**
 * Sets the current verbosity (logging) level.
 *
 * @param klass The Iodine module (VALUE)
 * @param num The log level (Fixnum, 0-5)
 * @return The new log level
 *
 * @note Raises TypeError if num is not a Fixnum.
 *
 * Ruby: Iodine.verbosity = n
 */
static VALUE iodine_verbosity_set(VALUE klass, VALUE num) {
  rb_check_type(num, RUBY_T_FIXNUM);
  FIO_LOG_LEVEL_SET(RB_FIX2INT(num));
  return num;
}

/* *****************************************************************************
Secrets - Server Secret Key for Cryptographic Operations
***************************************************************************** */

/**
 * Returns the server's secret as a 64-byte binary string.
 *
 * The secret is used for cryptographic operations like session signing,
 * CSRF tokens, and other security-sensitive operations. It's derived
 * from the secret key set via `Iodine.secret=` or auto-generated.
 *
 * @param klass The Iodine module (VALUE)
 * @return 64-byte binary String containing the server secret
 *
 * Ruby: Iodine.secret
 */
static VALUE iodine_secret(VALUE klass) {
  fio_u512 s = fio_secret();
  return rb_usascii_str_new((const char *)s.u8, sizeof(s));
  (void)klass;
}

/**
 * Sets a new server secret based on the provided key.
 *
 * The key is hashed/expanded to produce the internal 512-bit secret.
 * This should be set before starting the server for consistent
 * cryptographic operations across restarts.
 *
 * @param klass The Iodine module (VALUE)
 * @param key The secret key (String)
 * @return The new 64-byte server secret
 *
 * @note Raises TypeError if key is not a String.
 *
 * Ruby: Iodine.secret = "my-secret-key"
 */
static VALUE iodine_secret_set(VALUE klass, VALUE key) {
  rb_check_type(key, RUBY_T_STRING);
  fio_secret_set(RSTRING_PTR(key), RSTRING_LEN(key), 0);
  return iodine_secret(klass);
}

/* *****************************************************************************
Shutdown Timeouts - Graceful Shutdown Configuration
***************************************************************************** */

/**
 * Returns the current graceful shutdown timeout in milliseconds.
 *
 * During graceful shutdown, the server waits for active connections
 * to complete before forcing termination. This timeout controls
 * how long to wait.
 *
 * @param klass The Iodine module (VALUE)
 * @return Timeout in milliseconds as Fixnum
 *
 * Ruby: Iodine.shutdown_timeout
 */
static VALUE iodine_shutdown_timeout(VALUE klass) {
  return RB_SIZE2NUM(fio_io_shutdown_timeout());
  (void)klass;
}

/**
 * Sets the graceful shutdown timeout in milliseconds.
 *
 * Maximum allowed value is 5 minutes (300,000 ms).
 *
 * @param klass The Iodine module (VALUE)
 * @param num Timeout in milliseconds (Fixnum)
 * @return The new timeout value
 *
 * @note Raises TypeError if num is not a Fixnum.
 * @note Raises RangeError if num exceeds 5 minutes.
 *
 * Ruby: Iodine.shutdown_timeout = 30000  # 30 seconds
 */
static VALUE iodine_shutdown_timeout_set(VALUE klass, VALUE num) {
  rb_check_type(num, RUBY_T_FIXNUM);
  if (RB_NUM2SIZE(num) > (5U * 60U * 1000U))
    rb_raise(rb_eRangeError, "shutdown timeout out of range");
  fio_io_shutdown_timeout_set(RB_NUM2SIZE(num));
  return num;
}

#endif /* H___IODINE_CORE___H */
