#include "iodine.h"

#include <ruby/version.h>

#define FIO_INCLUDE_LINKED_LIST
#include "fio.h"
#include "fio_cli.h"
/* *****************************************************************************
OS specific patches
***************************************************************************** */

#ifdef __APPLE__
#include <dlfcn.h>
#endif

/** Any patches required by the running environment for consistent behavior */
static void patch_env(void) {
#ifdef __APPLE__
  /* patch for dealing with the High Sierra `fork` limitations */
  void *obj_c_runtime = dlopen("Foundation.framework/Foundation", RTLD_LAZY);
  (void)obj_c_runtime;
#endif
}

/* *****************************************************************************
Constants and State
***************************************************************************** */

VALUE IodineModule;
VALUE IodineBaseModule;

/** Default connection settings for {Iodine.listen} and {Iodine.connect}. */
VALUE iodine_default_args;

ID iodine_call_id;
ID iodine_to_s_id;

static VALUE address_sym;
static VALUE app_sym;
static VALUE body_sym;
static VALUE cookies_sym;
static VALUE handler_sym;
static VALUE headers_sym;
static VALUE log_sym;
static VALUE max_body_sym;
static VALUE max_clients_sym;
static VALUE max_headers_sym;
static VALUE max_msg_sym;
static VALUE method_sym;
static VALUE path_sym;
static VALUE ping_sym;
static VALUE port_sym;
static VALUE public_sym;
static VALUE service_sym;
static VALUE timeout_sym;
static VALUE tls_sym;
static VALUE url_sym;

/* *****************************************************************************
Idling
***************************************************************************** */

/* performs a Ruby state callback and clears the Ruby object's memory */
static void iodine_perform_on_idle_callback(void *blk_) {
  VALUE blk = (VALUE)blk_;
  IodineCaller.call(blk, iodine_call_id);
  IodineStore.remove(blk);
  fio_state_callback_remove(FIO_CALL_ON_IDLE, iodine_perform_on_idle_callback,
                            blk_);
}

/**
Schedules a single occuring event for the next idle cycle.

To schedule a reoccuring event, reschedule the event at the end of it's
run.

i.e.

      IDLE_PROC = Proc.new { puts "idle"; Iodine.on_idle &IDLE_PROC }
      Iodine.on_idle &IDLE_PROC
*/
static VALUE iodine_sched_on_idle(VALUE self) {
  // clang-format on
  rb_need_block();
  VALUE block = rb_block_proc();
  IodineStore.add(block);
  fio_state_callback_add(FIO_CALL_ON_IDLE, iodine_perform_on_idle_callback,
                         (void *)block);
  return block;
  (void)self;
}

/* *****************************************************************************
Running Iodine
***************************************************************************** */

typedef struct {
  int16_t threads;
  int16_t workers;
} iodine_start_params_s;

static void *iodine_run_outside_GVL(void *params_) {
  iodine_start_params_s *params = params_;
  fio_start(.threads = params->threads, .workers = params->workers);
  return NULL;
}

/* *****************************************************************************
Core API
***************************************************************************** */

/**
 * Returns the number of worker threads that will be used when {Iodine.start}
 * is called.
 *
 * Negative numbers are translated as fractions of the number of CPU cores.
 * i.e., -2 == half the number of detected CPU cores.
 *
 * Zero values promise nothing (iodine will decide what to do with them).
 *
 * @return [FixNum] Thread Count
 */
static VALUE iodine_threads_get(VALUE self) {
  VALUE i = rb_ivar_get(self, rb_intern2("@threads", 8));
  if (i == Qnil)
    i = INT2NUM(0);
  return i;
}

/**
 * Sets the number of worker threads that will be used when {Iodine.start}
 * is called.
 *
 * Negative numbers are translated as fractions of the number of CPU cores.
 * i.e., -2 == half the number of detected CPU cores.
 *
 * Zero values promise nothing (iodine will decide what to do with them).
 *
 * @param thread_count [FixNum] The number of worker threads to use
 */
static VALUE iodine_threads_set(VALUE self, VALUE val) {
  Check_Type(val, T_FIXNUM);
  if (NUM2SSIZET(val) >= (1 << 12)) {
    rb_raise(rb_eRangeError, "requsted thread count is out of range.");
  }
  rb_ivar_set(self, rb_intern2("@threads", 8), val);
  return val;
}

/**
 * Gets the logging level used for Iodine messages.
 *
 * Levels range from 0-5, where:
 *
 * 0 == Quite (no messages)
 * 1 == Fatal Errors only.
 * 2 == Errors only (including fatal errors).
 * 3 == Warnings and errors only.
 * 4 == Informational messages, warnings and errors (default).
 * 5 == Everything, including debug information.
 *
 * Logging is always performed to the process's STDERR and can be piped away.
 *
 * @return [FixNum] Logging Level
 *
 * NOTE: this does NOT effect HTTP logging.
 */
static VALUE iodine_logging_get(VALUE self) {
  return INT2FIX(FIO_LOG_LEVEL);
  (void)self;
}

/**
 * Gets the logging level used for Iodine messages.
 *
 * Levels range from 0-5, where:
 *
 * 0 == Quite (no messages)
 * 1 == Fatal Errors only.
 * 2 == Errors only (including fatal errors).
 * 3 == Warnings and errors only.
 * 4 == Informational messages, warnings and errors (default).
 * 5 == Everything, including debug information.
 *
 * Logging is always performed to the process's STDERR and can be piped away.
 *
 * @param log_level [FixNum] Sets the logging level
 *
 * NOTE: this does NOT effect HTTP logging.
 */
static VALUE iodine_logging_set(VALUE self, VALUE val) {
  Check_Type(val, T_FIXNUM);
  FIO_LOG_LEVEL = FIX2INT(val);
  return self;
}

/**
 * Returns the number of worker processes that will be used when {Iodine.start}
 * is called.
 *
 * Negative numbers are translated as fractions of the number of CPU cores.
 * i.e., -2 == half the number of detected CPU cores.
 *
 * Zero values promise nothing (iodine will decide what to do with them).
 *
 * 1 == single process mode, the msater process acts as a worker process.
 *
 * @return [FixNum] Worker Count
 */
static VALUE iodine_workers_get(VALUE self) {
  VALUE i = rb_ivar_get(self, rb_intern2("@workers", 8));
  if (i == Qnil)
    i = INT2NUM(0);
  return i;
}

/**
 * Sets the number of worker processes that will be used when {Iodine.start}
 * is called.
 *
 * Negative numbers are translated as fractions of the number of CPU cores.
 * i.e., -2 == half the number of detected CPU cores.
 *
 * Zero values promise nothing (iodine will decide what to do with them).
 *
 * 1 == single process mode, the msater process acts as a worker process.
 *
 * @param worker_count [FixNum] Number of worker processes
 */
static VALUE iodine_workers_set(VALUE self, VALUE val) {
  Check_Type(val, T_FIXNUM);
  if (NUM2SSIZET(val) >= (1 << 9)) {
    rb_raise(rb_eRangeError, "requsted worker process count is out of range.");
  }
  rb_ivar_set(self, rb_intern2("@workers", 8), val);
  return val;
}

/** Logs the Iodine startup message */
static void iodine_print_startup_message(iodine_start_params_s params) {
  VALUE iodine_version = rb_const_get(IodineModule, rb_intern("VERSION"));
  VALUE ruby_version = rb_const_get(IodineModule, rb_intern("RUBY_VERSION"));
  fio_expected_concurrency(&params.threads, &params.workers);
  FIO_LOG_INFO("Starting up Iodine:\n"
               " * Iodine %s\n * Ruby %s\n"
               " * facil.io " FIO_VERSION_STRING " (%s)\n"
               " * %d Workers X %d Threads per worker.\n"
               " * Maximum %zu open files / sockets per worker.\n"
               " * Master (root) process: %d.\n",
               StringValueCStr(iodine_version), StringValueCStr(ruby_version),
               fio_engine(), params.workers, params.threads, fio_capa(),
               fio_parent_pid());
  (void)params;
}

/**
 * This will block the calling (main) thread and start the Iodine reactor.
 *
 * When using cluster mode (2 or more worker processes), it is important that no
 * other threads are active.
 *
 * For many reasons, `fork` should NOT be called while multi-threading, so
 * cluster mode must always be initiated from the main thread in a single thread
 * environment.
 *
 * For information about why forking in multi-threaded environments should be
 * avoided, see (for example):
 * http://www.linuxprogrammingblog.com/threads-and-fork-think-twice-before-using-them
 *
 */
static VALUE iodine_start(VALUE self) {
  if (fio_is_running()) {
    rb_raise(rb_eRuntimeError, "Iodine already running!");
  }
  IodineCaller.set_GVL(1);
  VALUE threads_rb = iodine_threads_get(self);
  VALUE workers_rb = iodine_workers_get(self);
  iodine_start_params_s params = {
      .threads = NUM2SHORT(threads_rb),
      .workers = NUM2SHORT(workers_rb),
  };
  iodine_print_startup_message(params);
  IodineCaller.leaveGVL(iodine_run_outside_GVL, &params);
  return self;
}

/**
 * This will stop the iodine server, shutting it down.
 *
 * If called within a worker process (rather than the root/master process), this
 * will cause a hot-restart for the worker.
 */
static VALUE iodine_stop(VALUE self) {
  fio_stop();
  return self;
}

/**
 * Returns `true` if this process is the master / root process, `false`
 * otherwise.
 *
 * Note that the master process might be a worker process as well, when running
 * in single process mode (see {Iodine.workers}).
 */
static VALUE iodine_master_is(VALUE self) {
  return fio_is_master() ? Qtrue : Qfalse;
}

/**
 * Returns `true` if this process is a worker process or if iodine is running in
 * a single process mode (the master is also a worker), `false` otherwise.
 */
static VALUE iodine_worker_is(VALUE self) {
  return fio_is_master() ? Qtrue : Qfalse;
}

/**
 * Returns `true` if Iodine is currently running a server
 */
static VALUE iodine_running(VALUE self) {
  if (fio_is_running()) {
    return Qtrue;
  } else {
    return Qfalse;
  }
}

/* *****************************************************************************
CLI parser (Ruby's OptParser is more limiting than I knew...)
***************************************************************************** */

/**
 * Parses the CLI argnumnents, returning the Rack filename (if provided).
 *
 * Unknown arguments are ignored.
 *
 * @params [String] desc a String containg the iodine server's description.
 */
static VALUE iodine_cli_parse(VALUE self) {
  (void)self;
  VALUE ARGV = rb_get_argv();
  VALUE ret = Qtrue;
  VALUE defaults = iodine_default_args;
  VALUE iodine_version = rb_const_get(IodineModule, rb_intern("VERSION"));
  char desc[1024];
  if (!defaults || !ARGV || TYPE(ARGV) != T_ARRAY || TYPE(defaults) != T_HASH ||
      TYPE(iodine_version) != T_STRING || RSTRING_LEN(iodine_version) > 512) {
    FIO_LOG_ERROR("CLI parsing initialization error "
                  "ARGV=%p, Array?(%d), defaults == %p (%d)",
                  (void *)ARGV, (int)(TYPE(ARGV) == T_ARRAY), (void *)defaults,
                  (int)(TYPE(defaults) == T_HASH));
    return Qnil;
  }
  /* Copy the Ruby ARGV to a C valid ARGV */
  int argc = (int)RARRAY_LEN(ARGV) + 1;
  if (argc <= 1) {
    FIO_LOG_DEBUG("CLI: No arguments to parse...\n");
    return Qnil;
  } else {
    FIO_LOG_DEBUG("Iodine CLI parsing %d arguments", argc);
  }
  char **argv = calloc(argc, sizeof(*argv));
  FIO_ASSERT_ALLOC(argv);
  argv[0] = (char *)"iodine";
  for (int i = 1; i < argc; ++i) {
    VALUE tmp = rb_ary_entry(ARGV, (long)(i - 1));
    if (TYPE(tmp) != T_STRING) {
      FIO_LOG_ERROR("ARGV Array contains a non-String object.");
      ret = Qnil;
      goto finish;
    }
    fio_str_info_s s = IODINE_RSTRINFO(tmp);
    argv[i] = malloc(s.len + 1);
    FIO_ASSERT_ALLOC(argv[i]);
    memcpy(argv[i], s.data, s.len);
    argv[i][s.len] = 0;
  }
  /* Levarage the facil.io CLI library */
  memcpy(desc, "Iodine's HTTP/WebSocket server version ", 39);
  memcpy(desc + 39, StringValueCStr(iodine_version),
         RSTRING_LEN(iodine_version));
  memcpy(desc + 39 + RSTRING_LEN(iodine_version),
         "\r\n\r\nUse:\r\n    iodine <options> <filename>\r\n\r\n"
         "Both <options> and <filename> are optional. i.e.,:\r\n"
         "    iodine -p 0 -b /tmp/my_unix_sock\r\n"
         "    iodine -p 8080 path/to/app/conf.ru\r\n"
         "    iodine -p 8080 -w 4 -t 16\r\n"
         "    iodine -w -1 -t 4 -r redis://usr:pass@localhost:6379/",
         263);
  desc[39 + 263 + RSTRING_LEN(iodine_version)] = 0;
  fio_cli_start(
      argc, (const char **)argv, 0, -1, desc,
      FIO_CLI_PRINT_HEADER("Address Binding:"),
      "-bind -b -address address to listen to. defaults to any available.",
      FIO_CLI_INT("-port -p port number to listen to. defaults port 3000"),
      FIO_CLI_PRINT("\t\t\x1B[4mNote\x1B[0m: to bind to a Unix socket, set "
                    "\x1B[1mport\x1B[0m to 0."),
      FIO_CLI_PRINT_HEADER("Concurrency:"),
      FIO_CLI_INT("-threads -t number of threads per process."),
      FIO_CLI_INT("-workers -w number of processes to use."),
      FIO_CLI_PRINT("Negative concurrency values "
                    "map to fractions of available CPU cores."),
      FIO_CLI_PRINT_HEADER("HTTP Settings:"),
      FIO_CLI_STRING("-public -www public folder, for static file service."),
      FIO_CLI_INT("-keep-alive -k -tout HTTP keep-alive timeout in seconds "
                  "(0..255). Default: 40s"),
      FIO_CLI_BOOL("-log -v HTTP request logging."),
      FIO_CLI_INT(
          "-max-body -maxbd HTTP upload limit in Mega-Bytes. Default: 50Mb"),
      FIO_CLI_INT("-max-header -maxhd header limit per HTTP request in Kb. "
                  "Default: 32Kb."),
      FIO_CLI_PRINT_HEADER("WebSocket Settings:"),
      FIO_CLI_INT("-max-msg -maxms incoming WebSocket message limit in Kb. "
                  "Default: 250Kb"),
      FIO_CLI_INT("-ping websocket ping interval (1..255). Default: 40s"),
      FIO_CLI_PRINT_HEADER("SSL/TLS:"),
      FIO_CLI_BOOL("-tls enable SSL/TLS using a self-signed certificate."),
      FIO_CLI_STRING(
          "-tls-cert -cert the SSL/TLS public certificate file name."),
      FIO_CLI_STRING("-tls-key -key the SSL/TLS private key file name."),
      FIO_CLI_STRING(
          "-tls-pass -tls-password the password (if any) protecting the "
          "private key file."),
      FIO_CLI_PRINT("\t\t\x1B[1m-tls-password\x1B[0m is deprecated, use "
                    "\x1B[1m-tls-pass\x1B[0m"),
      FIO_CLI_PRINT_HEADER("Connecting Iodine to Redis:"),
      FIO_CLI_STRING(
          "-redis -r an optional Redis URL server address. Default: none."),
      FIO_CLI_INT(
          "-redis-ping -rp websocket ping interval (0..255). Default: 300s"),
      FIO_CLI_PRINT_HEADER("Misc:"),
      FIO_CLI_STRING("-config -C configuration file to be loaded."),
      FIO_CLI_STRING("-pid -pidfile name for the pid file to be created."),
      FIO_CLI_INT("-verbosity -V 0..5 server verbosity level. Default: 4"),
      FIO_CLI_BOOL(
          "-warmup --preload warm up the application. CAREFUL! with workers."));

  /* copy values from CLI library to iodine */
  if (fio_cli_get("-V")) {
    int level = fio_cli_get_i("-V");
    if (level > 0 && level < 100)
      FIO_LOG_LEVEL = level;
  }
  if (!fio_cli_get("-w") && getenv("WEB_CONCURRENCY")) {
    fio_cli_set("-w", getenv("WEB_CONCURRENCY"));
  }
  if (!fio_cli_get("-w") && getenv("WORKERS")) {
    fio_cli_set("-w", getenv("WORKERS"));
  }
  if (fio_cli_get("-w")) {
    iodine_workers_set(IodineModule, INT2NUM(fio_cli_get_i("-w")));
  }
  if (!fio_cli_get("-t") && getenv("THREADS")) {
    fio_cli_set("-t", getenv("THREADS"));
  }
  if (fio_cli_get("-t")) {
    iodine_threads_set(IodineModule, INT2NUM(fio_cli_get_i("-t")));
  }
  if (fio_cli_get_bool("-v")) {
    rb_hash_aset(defaults, log_sym, Qtrue);
  }
  if (fio_cli_get_bool("-warmup")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("warmup_")), Qtrue);
  }
  // if (!fio_cli_get("-b") && getenv("ADDRESS")) {
  //   fio_cli_set("-b", getenv("ADDRESS"));
  // }
  if (fio_cli_get("-b")) {
    if (fio_cli_get("-b")[0] == '/' ||
        (fio_cli_get("-b")[0] == '.' && fio_cli_get("-b")[1] == '/')) {
      if (fio_cli_get("-p") &&
          (fio_cli_get("-p")[0] != '0' || fio_cli_get("-p")[1])) {
        FIO_LOG_WARNING(
            "Detected a Unix socket binding (-b) conflicting with port.\n"
            "            Port settings (-p %s) are ignored",
            fio_cli_get("-p"));
      }
      fio_cli_set("-p", "0");
    } else {
      // if (!fio_cli_get("-p") && getenv("PORT")) {
      //   fio_cli_set("-p", getenv("PORT"));
      // }
    }
    rb_hash_aset(defaults, address_sym, rb_str_new_cstr(fio_cli_get("-b")));
  }
  if (fio_cli_get("-p")) {
    rb_hash_aset(defaults, port_sym, rb_str_new_cstr(fio_cli_get("-p")));
  }
  if (fio_cli_get("-www")) {
    rb_hash_aset(defaults, public_sym, rb_str_new_cstr(fio_cli_get("-www")));
  }
  if (!fio_cli_get("-redis") && getenv("IODINE_REDIS_URL")) {
    fio_cli_set("-redis", getenv("IODINE_REDIS_URL"));
  }
  if (fio_cli_get("-redis")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("redis_")),
                 rb_str_new_cstr(fio_cli_get("-redis")));
  }
  if (fio_cli_get("-k")) {
    rb_hash_aset(defaults, timeout_sym, INT2NUM(fio_cli_get_i("-k")));
  }
  if (fio_cli_get("-ping")) {
    rb_hash_aset(defaults, ping_sym, INT2NUM(fio_cli_get_i("-ping")));
  }
  if (fio_cli_get("-redis-ping")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("redis_ping_")),
                 INT2NUM(fio_cli_get_i("-redis-ping")));
  }
  if (fio_cli_get("-max-body")) {
    rb_hash_aset(defaults, max_body_sym,
                 INT2NUM((fio_cli_get_i("-max-body") /* * 1024 * 1024 */)));
  }
  if (fio_cli_get("-maxms")) {
    rb_hash_aset(defaults, max_msg_sym,
                 INT2NUM((fio_cli_get_i("-maxms") /* * 1024 */)));
  }
  if (fio_cli_get("-maxhd")) {
    rb_hash_aset(defaults, max_headers_sym,
                 INT2NUM((fio_cli_get_i("-maxhd") /* * 1024 */)));
  }
  if (fio_cli_get_bool("-tls") || fio_cli_get("-key") || fio_cli_get("-cert")) {
    VALUE rbtls = IodineCaller.call(IodineTLSClass, rb_intern2("new", 3));
    if (rbtls == Qnil) {
      FIO_LOG_FATAL("Iodine internal error, Ruby TLS object is nil.");
      exit(-1);
    }
    fio_tls_s *tls = iodine_tls2c(rbtls);
    if (!tls) {
      FIO_LOG_FATAL("Iodine internal error, TLS object NULL.");
      exit(-1);
    }
    if (fio_cli_get("-tls-key") && fio_cli_get("-tls-cert")) {
      fio_tls_cert_add(tls, NULL, fio_cli_get("-tls-cert"),
                       fio_cli_get("-tls-key"), fio_cli_get("-tls-pass"));
    } else {
      if (!fio_cli_get_bool("-tls"))
        FIO_LOG_ERROR("TLS support requires both key and certificate."
                      "\r\n\t\tfalling back on a self signed certificate.");
      char name[1024];
      fio_local_addr(name, 1024);
      fio_tls_cert_add(tls, name, NULL, NULL, NULL);
    }
    rb_hash_aset(defaults, tls_sym, rbtls);
  }
  if (fio_cli_unnamed_count()) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("filename_")),
                 rb_str_new_cstr(fio_cli_unnamed(0)));
  }
  if (fio_cli_get("-pid")) {
    VALUE pid_filename = rb_str_new_cstr(fio_cli_get("-pid"));
    rb_hash_aset(defaults, ID2SYM(rb_intern("pid_")), pid_filename);
    rb_hash_aset(defaults, ID2SYM(rb_intern("pid")), pid_filename);
  }
  if (fio_cli_get("-config")) {
    VALUE conf_filename = rb_str_new_cstr(fio_cli_get("-config"));
    rb_hash_aset(defaults, ID2SYM(rb_intern("conf_")), conf_filename);
  }

  /* create `filename` String, cleanup and return */
  fio_cli_end();
finish:
  for (int i = 1; i < argc; ++i) {
    free(argv[i]);
  }
  free(argv);
  return ret;
}

/* *****************************************************************************
Argument support for `connect` / `listen`
***************************************************************************** */

static int for_each_header_value(VALUE key, VALUE val, VALUE h_) {
  FIOBJ h = h_;
  if (RB_TYPE_P(key, T_SYMBOL))
    key = rb_sym2str(key);
  if (!RB_TYPE_P(key, T_STRING)) {
    FIO_LOG_WARNING("invalid key type in header hash, ignored.");
    return ST_CONTINUE;
  }
  if (RB_TYPE_P(val, T_SYMBOL))
    val = rb_sym2str(val);
  if (RB_TYPE_P(val, T_STRING)) {
    FIOBJ k = fiobj_str_new(RSTRING_PTR(key), RSTRING_LEN(key));
    fiobj_hash_set(h, k, fiobj_str_new(RSTRING_PTR(val), RSTRING_LEN(val)));
    fiobj_free(k);
  } else if (RB_TYPE_P(val, T_ARRAY)) {
    FIOBJ k = fiobj_str_new(RSTRING_PTR(key), RSTRING_LEN(key));
    size_t len = rb_array_len(val);
    FIOBJ v = fiobj_ary_new2(len);
    fiobj_hash_set(h, k, v);
    fiobj_free(k);
    for (size_t i = 0; i < len; ++i) {
      VALUE tmp = rb_ary_entry(val, i);
      if (RB_TYPE_P(tmp, T_SYMBOL))
        tmp = rb_sym2str(tmp);
      if (RB_TYPE_P(tmp, T_STRING))
        fiobj_ary_push(v, fiobj_str_new(RSTRING_PTR(tmp), RSTRING_LEN(tmp)));
    }
  } else {
    FIO_LOG_WARNING("invalid header value type, ignored.");
  }
  return ST_CONTINUE;
}

static int for_each_cookie(VALUE key, VALUE val, VALUE h_) {
  FIOBJ h = h_;
  if (RB_TYPE_P(key, T_SYMBOL))
    key = rb_sym2str(key);
  if (!RB_TYPE_P(key, T_STRING)) {
    FIO_LOG_WARNING("invalid key type in cookie hash, ignored.");
    return ST_CONTINUE;
  }
  if (RB_TYPE_P(val, T_SYMBOL))
    val = rb_sym2str(val);
  if (RB_TYPE_P(val, T_STRING)) {
    FIOBJ k = fiobj_str_new(RSTRING_PTR(key), RSTRING_LEN(key));
    fiobj_hash_set(h, k, fiobj_str_new(RSTRING_PTR(val), RSTRING_LEN(val)));
    fiobj_free(k);
  } else {
    FIO_LOG_WARNING("invalid cookie value type, ignored.");
  }
  return ST_CONTINUE;
}

/* cleans up any resources used by the argument list processing */
FIO_FUNC void iodine_connect_args_cleanup(iodine_connection_args_s *s) {
  if (!s)
    return;
  fiobj_free(s->cookies);
  fiobj_free(s->headers);
  if (s->port.capa)
    fio_free(s->port.data);
  if (s->address.capa)
    fio_free(s->address.data);
  if (s->tls)
    fio_tls_destroy(s->tls);
}

/*
Accepts:

     func(settings)

Supported Settigs:
- `:url`
- `:handler` (deprecated: `app`)
- `:service` (raw / ws / wss / http / https )
- `:address`
- `:port`
- `:path` (HTTP/WebSocket client)
- `:method` (HTTP client)
- `:headers` (HTTP/WebSocket client)
- `:cookies` (HTTP/WebSocket client)
- `:body` (HTTP client)
- `:tls`
- `:log` (HTTP only)
- `:public` (public folder, HTTP server only)
- `:timeout` (HTTP only)
- `:ping` (`:raw` clients and WebSockets only)
- `:max_headers` (HTTP only)
- `:max_body` (HTTP only)
- `:max_msg` (WebSockets only)

*/
FIO_FUNC iodine_connection_args_s iodine_connect_args(VALUE s, uint8_t is_srv) {
  Check_Type(s, T_HASH);
  iodine_connection_args_s r = {.ping = 0}; /* set all to 0 */
  /* Collect argument values */
  VALUE address = rb_hash_aref(s, address_sym);
  VALUE app = rb_hash_aref(s, app_sym);
  VALUE body = rb_hash_aref(s, body_sym);
  VALUE cookies = rb_hash_aref(s, cookies_sym);
  VALUE handler = rb_hash_aref(s, handler_sym);
  VALUE headers = rb_hash_aref(s, headers_sym);
  VALUE log = rb_hash_aref(s, log_sym);
  VALUE max_body = rb_hash_aref(s, max_body_sym);
  VALUE max_clients = rb_hash_aref(s, max_clients_sym);
  VALUE max_headers = rb_hash_aref(s, max_headers_sym);
  VALUE max_msg = rb_hash_aref(s, max_msg_sym);
  VALUE method = rb_hash_aref(s, method_sym);
  VALUE path = rb_hash_aref(s, path_sym);
  VALUE ping = rb_hash_aref(s, ping_sym);
  VALUE port = rb_hash_aref(s, port_sym);
  VALUE r_public = rb_hash_aref(s, public_sym);
  VALUE service = rb_hash_aref(s, service_sym);
  VALUE timeout = rb_hash_aref(s, timeout_sym);
  VALUE tls = rb_hash_aref(s, tls_sym);
  VALUE r_url = rb_hash_aref(s, url_sym);
  fio_str_info_s service_str = {.data = NULL};

  /* Complete using default values */
  if (address == Qnil)
    address = rb_hash_aref(iodine_default_args, address_sym);
  if (app == Qnil)
    app = rb_hash_aref(iodine_default_args, app_sym);
  if (cookies == Qnil)
    cookies = rb_hash_aref(iodine_default_args, cookies_sym);
  if (handler == Qnil)
    handler = rb_hash_aref(iodine_default_args, handler_sym);
  if (headers == Qnil)
    headers = rb_hash_aref(iodine_default_args, headers_sym);
  if (log == Qnil)
    log = rb_hash_aref(iodine_default_args, log_sym);
  if (max_body == Qnil)
    max_body = rb_hash_aref(iodine_default_args, max_body_sym);
  if (max_clients == Qnil)
    max_clients = rb_hash_aref(iodine_default_args, max_clients_sym);
  if (max_headers == Qnil)
    max_headers = rb_hash_aref(iodine_default_args, max_headers_sym);
  if (max_msg == Qnil)
    max_msg = rb_hash_aref(iodine_default_args, max_msg_sym);
  if (method == Qnil)
    method = rb_hash_aref(iodine_default_args, method_sym);
  if (path == Qnil)
    path = rb_hash_aref(iodine_default_args, path_sym);
  if (ping == Qnil)
    ping = rb_hash_aref(iodine_default_args, ping_sym);
  if (port == Qnil)
    port = rb_hash_aref(iodine_default_args, port_sym);
  if (r_public == Qnil) {
    r_public = rb_hash_aref(iodine_default_args, public_sym);
  }
  // if (service == Qnil) // not supported by default settings...
  //   service = rb_hash_aref(iodine_default_args, service_sym);
  if (timeout == Qnil)
    timeout = rb_hash_aref(iodine_default_args, timeout_sym);
  if (tls == Qnil)
    tls = rb_hash_aref(iodine_default_args, tls_sym);

  /* TODO: deprecation */
  if (handler == Qnil) {
    handler = rb_hash_aref(s, app_sym);
    if (handler != Qnil)
      FIO_LOG_WARNING(":app is deprecated in Iodine.listen and Iodine.connect. "
                      "Use :handler");
  }

  /* specific for HTTP */
  if (is_srv && handler == Qnil && rb_block_given_p()) {
    handler = rb_block_proc();
  }

  /* Raise exceptions on errors (last chance) */
  if (handler == Qnil) {
    rb_raise(rb_eArgError, "a :handler is required.");
  }

  /* Set existing values */
  if (handler != Qnil) {
    r.handler = handler;
  }
  if (address != Qnil && RB_TYPE_P(address, T_STRING)) {
    r.address = IODINE_RSTRINFO(address);
  }
  if (body != Qnil && RB_TYPE_P(body, T_STRING)) {
    r.body = IODINE_RSTRINFO(body);
  }
  if (cookies != Qnil && RB_TYPE_P(cookies, T_HASH)) {
    r.cookies = fiobj_hash_new2(rb_hash_size(cookies));
    rb_hash_foreach(cookies, for_each_cookie, r.cookies);
  }
  if (headers != Qnil && RB_TYPE_P(headers, T_HASH)) {
    r.headers = fiobj_hash_new2(rb_hash_size(headers));
    rb_hash_foreach(headers, for_each_header_value, r.headers);
  }
  if (log != Qnil && log != Qfalse) {
    r.log = 1;
  }
  if (max_body != Qnil && RB_TYPE_P(max_body, T_FIXNUM)) {
    r.max_body = FIX2ULONG(max_body) * 1024 * 1024;
  }
  if (max_clients != Qnil && RB_TYPE_P(max_clients, T_FIXNUM)) {
    r.max_clients = FIX2ULONG(max_clients);
  }
  if (max_headers != Qnil && RB_TYPE_P(max_headers, T_FIXNUM)) {
    r.max_headers = FIX2ULONG(max_headers) * 1024;
  }
  if (max_msg != Qnil && RB_TYPE_P(max_msg, T_FIXNUM)) {
    r.max_msg = FIX2ULONG(max_msg) * 1024;
  }
  if (method != Qnil && RB_TYPE_P(method, T_STRING)) {
    r.method = IODINE_RSTRINFO(method);
  }
  if (path != Qnil && RB_TYPE_P(path, T_STRING)) {
    r.path = IODINE_RSTRINFO(path);
  }
  if (ping != Qnil && RB_TYPE_P(ping, T_FIXNUM)) {
    if (FIX2ULONG(ping) > 255)
      FIO_LOG_WARNING(":ping value over 255 will be silently ignored.");
    else
      r.ping = FIX2ULONG(ping);
  }
  if (port != Qnil) {
    if (RB_TYPE_P(port, T_STRING)) {
      char *tmp = RSTRING_PTR(port);
      if (fio_atol(&tmp))
        r.port = IODINE_RSTRINFO(port);
    } else if (RB_TYPE_P(port, T_FIXNUM) && FIX2UINT(port)) {
      if (FIX2UINT(port) >= 65536) {
        FIO_LOG_WARNING("Port number %u is too high, quietly ignored.",
                        FIX2UINT(port));
      } else {
        r.port = (fio_str_info_s){.data = fio_malloc(16), .len = 0, .capa = 1};
        r.port.len = fio_ltoa(r.port.data, FIX2INT(port), 10);
        r.port.data[r.port.len] = 0;
      }
    }
  }

  if (r_public != Qnil && RB_TYPE_P(r_public, T_STRING)) {
    r.public = IODINE_RSTRINFO(r_public);
  }
  if (service != Qnil && RB_TYPE_P(service, T_STRING)) {
    service_str = IODINE_RSTRINFO(service);
  } else if (service != Qnil && RB_TYPE_P(service, T_SYMBOL)) {
    service = rb_sym2str(service);
    service_str = IODINE_RSTRINFO(service);
  }
  if (timeout != Qnil && RB_TYPE_P(ping, T_FIXNUM)) {
    if (FIX2ULONG(timeout) > 255)
      FIO_LOG_WARNING(":timeout value over 255 will be silently ignored.");
    else
      r.timeout = FIX2ULONG(timeout);
  }
  if (tls != Qnil) {
    r.tls = iodine_tls2c(tls);
    if (r.tls)
      fio_tls_dup(r.tls);
  }
  /* URL parsing */
  if (r_url != Qnil && RB_TYPE_P(r_url, T_STRING)) {
    r.url = IODINE_RSTRINFO(r_url);
    fio_url_s u = fio_url_parse(r.url.data, r.url.len);
    /* set service string */
    if (u.scheme.data) {
      service_str = u.scheme;
    }
    /* copy port number */
    if (u.port.data) {
      char *tmp = u.port.data;
      if (fio_atol(&tmp) == 0) {
        if (r.port.capa)
          fio_free(r.port.data);
        r.port = (fio_str_info_s){.data = NULL};
      } else {
        if (u.port.len > 5)
          FIO_LOG_WARNING("Port number error (%.*s too long to be valid).",
                          (int)u.port.len, u.port.data);
        if (r.port.capa && u.port.len >= 16) {
          fio_free(r.port.data);
          r.port = (fio_str_info_s){.data = NULL};
        }
        if (!r.port.capa)
          r.port = (fio_str_info_s){
              .data = fio_malloc(u.port.len + 1), .len = u.port.len, .capa = 1};
        memcpy(r.port.data, u.port.data, u.port.len);
        r.port.len = u.port.len;
        r.port.data[r.port.len] = 0;
      }
    } else {
      if (r.port.capa)
        fio_free(r.port.data);
      r.port = (fio_str_info_s){.data = NULL};
    }
    /* copy host / address */
    if (u.host.data) {
      r.address = (fio_str_info_s){
          .data = fio_malloc(u.host.len + 1), .len = u.host.len, .capa = 1};
      memcpy(r.address.data, u.host.data, u.host.len);
      r.address.len = u.host.len;
      r.address.data[r.address.len] = 0;
    } else {
      if (r.address.capa)
        fio_free(r.address.data);
      r.address = (fio_str_info_s){.data = NULL};
    }
    /* set path */
    if (u.path.data) {
      /* support possible Unix address as "raw://:0/my/sock.sock" */
      if (r.address.data || r.port.data)
        r.path = u.path;
      else
        r.address = u.path;
    }
  }
  /* test/set service type */
  r.service = IODINE_SERVICE_RAW;
  if (service_str.data) {
    switch (service_str.data[0]) {
    case 't': /* overflow */
              /* tcp or tls */
      if (service_str.data[1] == 'l' && !r.tls) {
        char *local = NULL;
        char buf[1024];
        buf[1023] = 0;
        if (is_srv) {
          local = buf;
          if (fio_local_addr(buf, 1023) >= 1022)
            local = NULL;
        }
        r.tls = fio_tls_new(local, NULL, NULL, NULL);
      }
      /* overflow */
    case 'u': /* overflow */
    /* unix */
    case 'r':
      /* raw */
      r.service = IODINE_SERVICE_RAW;
      break;
    case 'h':
      /* http(s) */
      r.service = IODINE_SERVICE_HTTP;
      if (service_str.len == 5 && !r.tls) {
        char *local = NULL;
        char buf[1024];
        buf[1023] = 0;
        if (is_srv) {
          local = buf;
          if (fio_local_addr(buf, 1023) >= 1022)
            local = NULL;
        }
        r.tls = fio_tls_new(local, NULL, NULL, NULL);
      }
    case 'w':
      /* ws(s) */
      r.service = IODINE_SERVICE_WS;
      if (service_str.len == 3 && !r.tls) {
        char *local = NULL;
        char buf[1024];
        buf[1023] = 0;
        if (is_srv) {
          local = buf;
          if (fio_local_addr(buf, 1023) >= 1022)
            local = NULL;
        }
        r.tls = fio_tls_new(local, NULL, NULL, NULL);
      }
      break;
    }
  }
  return r;
}

/* *****************************************************************************
Listen function routing
***************************************************************************** */

// clang-format off
/*
{Iodine.listen} can be used to listen to any incoming connections, including HTTP and raw (tcp/ip and unix sockets) connections.

     Iodine.listen(settings)

Supported Settigs:

|  |  |
|---|---|
| `:url` | URL indicating service type, host name and port. Path will be parsed as a Unix socket. |
| `:handler` | (deprecated: `:app`) see details below. |
| `:address` | an IP address or a unix socket address. Only relevant if `:url` is missing. |
| `:log` |  (HTTP only) request logging. For global verbosity see {Iodine.verbosity} |
| `:max_body` | (HTTP only) maximum upload size allowed per request before disconnection (in Mb). |
| `:max_headers` |  (HTTP only) maximum total header length allowed per request (in Kb). |
| `:max_msg` |  (WebSockets only) maximum message size pre message (in Kb). |
| `:ping` |  (`:raw` clients and WebSockets only) ping interval (in seconds). Up to 255 seconds. |
| `:port` | port number to listen to either a String or Number) |
| `:public` | (HTTP server only) public folder for static file service. |
| `:service` | (`:raw` / `:tls` / `:ws` / `:wss` / `:http` / `:https` ) a supported service this socket will listen to. |
| `:timeout` |  (HTTP only) keep-alive timeout in seconds. Up to 255 seconds. |
| `:tls` | an {Iodine::TLS} context object for encrypted connections. |

Some connection settings are only valid when listening to HTTP / WebSocket connections.

If `:url` is provided, it will overwrite the `:address` and `:port` settings (if provided).

For HTTP connections, the `:handler` **must** be a valid Rack application object (answers `.call(env)`).

Here's an example for an HTTP hello world application:

      require 'iodine'
      # a handler can be a block
      Iodine.listen(service: :http, port: "3000") {|env| [200, {"Content-Length" => "12"}, ["Hello World!"]] }
      # start the service
      Iodine.threads = 1
      Iodine.start


Here's another example, using a Unix Socket instead of a TCP/IP socket for an HTTP hello world application.

This example shows how the `:url` option can be used, but the `:address` settings could have been used for the same effect (with `port: 0`).

      require 'iodine'
      # note that unix sockets in URL form use an absolute path.
      Iodine.listen(url: "http://:0/tmp/sock.sock") {|env| [200, {"Content-Length" => "12"}, ["Hello World!"]] }
      # start the service
      Iodine.threads = 1
      Iodine.start


For raw connections, the `:handler` object should be an object that answer `.call` and returns a valid callback object that supports the following callbacks (see also {Iodine::Connection}):

|  |  |
|---|---|
| `on_open(client)` | called after a connection was established |
| `on_message(client,data)` | called when incoming data is available. Data may be fragmented. |
| `on_drained(client)` | called after pending `client.write` events have been processed (see {Iodine::Connection#pending}). |
| `ping(client)` | called whenever a timeout has occured (see {Iodine::Connection#timeout=}). |
| `on_shutdown(client)` | called if the server is shutting down. This is called before the connection is closed. |
| `on_close(client)` | called when the connection with the client was closed. |

The `client` argument passed to the `:handler` callbacks is an {Iodine::Connection} instance that represents the connection / the client.

Here's an example for a telnet based chat-room example:

      require 'iodine'
      # define the protocol for our service
      module ChatHandler
        def self.on_open(client)
          # Set a connection timeout
          client.timeout = 10
          # subscribe to the chat channel.
          client.subscribe :chat
          # Write a welcome message
          client.publish :chat, "new member entered the chat\r\n"
        end
        # this is called for incoming data - note data might be fragmented.
        def self.on_message(client, data)
          # publish the data we received
          client.publish :chat, data
          # close the connection when the time comes
          client.close if data =~ /^bye[\n\r]/
        end
        # called whenever timeout occurs.
        def self.ping(client)
          client.write "System: quite, isn't it...?\r\n"
        end
        # called if the connection is still open and the server is shutting down.
        def self.on_shutdown(client)
          # write the data we received
          client.write "Chat server going away. Try again later.\r\n"
        end
        # returns the callback object (self).
        def self.call
          self
        end
      end
      # we use can both the `handler` keyword or a block, anything that answers #call.
      Iodine.listen(service: :raw, port: "3000", handler: ChatHandler)
      # we can listen to more than a single socket at a time.
      Iodine.listen(url: "raw://:3030", handler: ChatHandler)
      # start the service
      Iodine.threads = 1
      Iodine.start



Returns the handler object used.
*/
static VALUE iodine_listen(VALUE self, VALUE args) {
  // clang-format on
  iodine_connection_args_s s = iodine_connect_args(args, 1);
  intptr_t uuid = -1;
  switch (s.service) {
  case IODINE_SERVICE_RAW:
    uuid = iodine_tcp_listen(s);
    break;
  case IODINE_SERVICE_HTTP: /* overflow */
  case IODINE_SERVICE_WS:
    uuid = iodine_http_listen(s);
    break;
  }
  iodine_connect_args_cleanup(&s);
  if (uuid == -1)
    rb_raise(rb_eRuntimeError, "Couldn't open listening socket.");
  return s.handler;
  (void)self;
}

/* *****************************************************************************
Connect function routing
***************************************************************************** */

// clang-format off
/*

The {connect} method instructs iodine to connect to a server using either TCP/IP or Unix sockets.

     Iodine.connect(settings)

Supported Settigs:


|  |  |
|---|---|
| `:url` | URL indicating service type, host name, port and optional path. |
| `:handler` | see details below. |
| `:address` | an IP address or a unix socket address. Only relevant if `:url` is missing. |
| `:body` | (HTTP client) the body to be sent. |
| `:cookies` | (HTTP/WebSocket client) cookie data. |
| `:headers` | (HTTP/WebSocket client) custom headers. |
| `:log` | (HTTP only) - logging the requests. |
| `:max_body` | (HTTP only) - limits HTTP body in the response, see {listen}. |
| `:max_headers` | (HTTP only) - limits the header length in the response, see {listen}. |
| `:max_msg` |  (WebSockets only) maximum incoming message size pre message (in Kb). |
| `:method` | (HTTP client) a String such as "GET" or "POST". |
| `:path` |HTTP/WebSocket client) the HTTP path to be used. |
| `:ping` | ping interval (in seconds). Up to 255 seconds. |
| `:port` | port number to listen to either a String or Number) |
| `:public` | (public folder, HTTP server only) |
| `:service` | (`:raw` / `:tls` / `:ws` / `:wss` ) |
| `:timeout` | (HTTP only) keep-alive timeout in seconds. Up to 255 seconds. |
| `:tls` | an {Iodine::TLS} context object for encrypted connections. |

Some connection settings are only valid for HTTP / WebSocket connections.

If `:url` is provided, it will overwrite the `:address`, `:port` and `:path` settings (if provided).

Unlike {Iodine.listen}, a block can't be used and a `:handler` object **must** be provided.

If the connection fails, only the `on_close` callback will be called (with a `nil` client).

Here's an example TCP/IP client that sends a simple HTTP GET request:

      # use a secure connection?
      USE_TLS = false

      # remote server details
      $port = USE_TLS ? 443 : 80
      $address = "google.com"


      # require iodine
      require 'iodine'

      # Iodine runtime settings
      Iodine.threads = 1
      Iodine.workers = 1
      Iodine.verbosity = 3 # warnings only


      # a client callback handler
      module Client

        def self.on_open(connection)
          # Set a connection timeout
          connection.timeout = 10
          # subscribe to the chat channel.
          puts "* Sending request..."
          connection.write "GET / HTTP/1.1\r\nHost: #{$address}\r\n\r\n"
        end

        def self.on_message(connection, data)
          # publish the data we received
          STDOUT.write data
          # close the connection after a second... we're not really parsing anything, so it's a guess.
          Iodine.run_after(1000) { connection.close }
        end

        def self.on_close(connection)
          # stop iodine
          Iodine.stop
          puts "Done."
        end

        # returns the callback object (self).
        def self.call
          self
        end
      end



      if(USE_TLS)
        tls = Iodine::TLS.new
        # ALPN blocks should return a valid calback object
        tls.on_protocol("http/1.1") { Client }
      end

      Iodine.connect(address: $address, port: $port, handler: Client, tls: tls)

      # start the iodine reactor
      Iodine.start

Iodine also supports WebSocket client connections, using either the `url` property or the `ws` and `wss` service names.

The following example establishes a secure (TLS) connects to the WebSocket echo testing server at `wss://echo.websocket.org`:

      # require iodine
      require 'iodine'

      # The client class
      class EchoClient

        def on_open(connection)
          @messages = [ "Hello World!",
            "I'm alive and sending messages",
            "I also receive messages",
            "now that we all know this...",
            "I can stop.",
            "Goodbye." ]
          send_one_message(connection)
        end

        def on_message(connection, message)
          puts "Received: #{message}"
          send_one_message(connection)
        end

        def on_close(connection)
          # in this example, we stop iodine once the client is closed
          puts "* Client closed."
          Iodine.stop
        end

        # We use this method to pop messages from the queue and send them
        #
        # When the queue is empty, we disconnect the client.
        def send_one_message(connection)
          msg = @messages.shift
          if(msg)
            connection.write msg
          else
            connection.close
          end
        end
      end

      Iodine.threads = 1
      Iodine.connect url: "wss://echo.websocket.org", handler: EchoClient.new, ping: 40
      Iodine.start

**Note**: the `on_close` callback is always called, even if a connection couldn't be established.

Returns the handler object used.
*/
static VALUE iodine_connect(VALUE self, VALUE args) {
  // clang-format on
  iodine_connection_args_s s = iodine_connect_args(args, 0);
  intptr_t uuid = -1;
  switch (s.service) {
  case IODINE_SERVICE_RAW:
    uuid = iodine_tcp_connect(s);
    break;
  case IODINE_SERVICE_HTTP:
    iodine_connect_args_cleanup(&s);
    rb_raise(rb_eRuntimeError, "HTTP client connections aren't supported yet.");
    return Qnil;
    break;
  case IODINE_SERVICE_WS:
    uuid = iodine_ws_connect(s);
    break;
  }
  iodine_connect_args_cleanup(&s);
  if (uuid == -1)
    rb_raise(rb_eRuntimeError, "Couldn't open client socket.");
  return self;
}

/* *****************************************************************************
Ruby loads the library and invokes the Init_<lib_name> function...

Here we connect all the C code to the Ruby interface, completing the bridge
between Lib-Server and Ruby.
***************************************************************************** */
void Init_iodine(void) {
  /* common Symbol objects in use by Iodine */
#define IODINE_MAKE_SYM(name)                                                  \
  do {                                                                         \
    name##_sym = rb_id2sym(rb_intern(#name));                                  \
    rb_global_variable(&name##_sym);                                           \
  } while (0)
  IODINE_MAKE_SYM(address);
  IODINE_MAKE_SYM(app);
  IODINE_MAKE_SYM(body);
  IODINE_MAKE_SYM(cookies);
  IODINE_MAKE_SYM(handler);
  IODINE_MAKE_SYM(headers);
  IODINE_MAKE_SYM(log);
  IODINE_MAKE_SYM(max_body);
  IODINE_MAKE_SYM(max_clients);
  IODINE_MAKE_SYM(max_headers);
  IODINE_MAKE_SYM(max_msg);
  IODINE_MAKE_SYM(method);
  IODINE_MAKE_SYM(path);
  IODINE_MAKE_SYM(ping);
  IODINE_MAKE_SYM(port);
  IODINE_MAKE_SYM(public);
  IODINE_MAKE_SYM(service);
  IODINE_MAKE_SYM(timeout);
  IODINE_MAKE_SYM(tls);
  IODINE_MAKE_SYM(url);

  // load any environment specific patches
  patch_env();

  // force the GVL state for the main thread
  IodineCaller.set_GVL(1);

  // Create the Iodine module (namespace)
  IodineModule = rb_define_module("Iodine");
  IodineBaseModule = rb_define_module_under(IodineModule, "Base");
  VALUE IodineCLIModule = rb_define_module_under(IodineBaseModule, "CLI");
  iodine_call_id = rb_intern2("call", 4);
  iodine_to_s_id = rb_intern("to_s");

  // register core methods
  rb_define_module_function(IodineModule, "threads", iodine_threads_get, 0);
  rb_define_module_function(IodineModule, "threads=", iodine_threads_set, 1);
  rb_define_module_function(IodineModule, "verbosity", iodine_logging_get, 0);
  rb_define_module_function(IodineModule, "verbosity=", iodine_logging_set, 1);
  rb_define_module_function(IodineModule, "workers", iodine_workers_get, 0);
  rb_define_module_function(IodineModule, "workers=", iodine_workers_set, 1);
  rb_define_module_function(IodineModule, "start", iodine_start, 0);
  rb_define_module_function(IodineModule, "stop", iodine_stop, 0);
  rb_define_module_function(IodineModule, "on_idle", iodine_sched_on_idle, 0);
  rb_define_module_function(IodineModule, "master?", iodine_master_is, 0);
  rb_define_module_function(IodineModule, "worker?", iodine_worker_is, 0);
  rb_define_module_function(IodineModule, "running?", iodine_running, 0);
  rb_define_module_function(IodineModule, "listen", iodine_listen, 1);
  rb_define_module_function(IodineModule, "connect", iodine_connect, 1);

  // register CLI methods
  rb_define_module_function(IodineCLIModule, "parse", iodine_cli_parse, 0);

  /** Default connection settings for {listen} and {connect}. */
  iodine_default_args = rb_hash_new();
  /** Default connection settings for {listen} and {connect}. */
  rb_const_set(IodineModule, rb_intern("DEFAULT_SETTINGS"),
               iodine_default_args);

  /** Depracated, use {Iodine::DEFAULT_SETTINGS}. */
  rb_const_set(IodineModule, rb_intern("DEFAULT_HTTP_ARGS"),
               iodine_default_args);

  // initialize Object storage for GC protection
  iodine_storage_init();

  // initialize concurrency related methods
  iodine_defer_initialize();

  // initialize the connection class
  iodine_connection_init();

  // intialize the TCP/IP related module
  iodine_init_tcp_connections();

  // initialize the HTTP module
  iodine_init_http();

  // initialize SSL/TLS support module
  iodine_init_tls();

  // initialize JSON helpers
  iodine_init_json();

  // initialize Mustache engine
  iodine_init_mustache();

  // initialize Rack helpers and IO
  iodine_init_helpers();
  IodineRackIO.init();

  // initialize Pub/Sub extension (for Engines)
  iodine_pubsub_init();
}
