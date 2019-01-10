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
static ID call_id;

/* *****************************************************************************
Idling
***************************************************************************** */

/* performs a Ruby state callback and clears the Ruby object's memory */
static void iodine_perform_on_idle_callback(void *blk_) {
  VALUE blk = (VALUE)blk_;
  IodineCaller.call(blk, call_id);
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
               " * Master (root) process: %d.\n",
               StringValueCStr(iodine_version), StringValueCStr(ruby_version),
               fio_engine(), params.workers, params.threads, fio_parent_pid());
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
      FIO_CLI_INT("-workers -w number of processes to use."),
      FIO_CLI_INT("-threads -t number of threads per process."),
      FIO_CLI_PRINT_HEADER("HTTP Settings:"),
      FIO_CLI_STRING("-public -www public folder, for static file service."),
      FIO_CLI_BOOL("-log -v HTTP request logging."),
      FIO_CLI_INT("-keep-alive -k -tout HTTP keep-alive timeout in seconds "
                  "(0..255). Default: 40s"),
      FIO_CLI_INT("-ping websocket ping interval (0..255). Default: 40s"),
      FIO_CLI_INT(
          "-max-body -maxbd HTTP upload limit in Mega-Bytes. Default: 50Mb"),
      FIO_CLI_INT("-max-header -maxhd header limit per HTTP request in Kb. "
                  "Default: 32Kb."),
      FIO_CLI_PRINT_HEADER("WebSocket Settings:"),
      FIO_CLI_INT("-max-msg -maxms incoming WebSocket message limit in Kb. "
                  "Default: 250Kb"),
      FIO_CLI_PRINT_HEADER("SSL/TLS:"),
      FIO_CLI_BOOL("-tls enable SSL/TLS using a self-signed certificate."),
      FIO_CLI_STRING(
          "-tls-cert -cert the SSL/TLS public certificate file name."),
      FIO_CLI_STRING("-tls-key -key the SSL/TLS private key file name."),
      FIO_CLI_STRING("-tls-password the password (if any) protecting the "
                     "private key file."),
      FIO_CLI_PRINT_HEADER("Connecting Iodine to Redis:"),
      FIO_CLI_STRING(
          "-redis -r an optional Redis URL server address. Default: none."),
      FIO_CLI_INT(
          "-redis-ping -rp websocket ping interval (0..255). Default: 300s"),
      FIO_CLI_PRINT_HEADER("Misc:"),
      FIO_CLI_BOOL(
          "-warmup --preload warm up the application. CAREFUL! with workers."),
      FIO_CLI_INT("-verbosity -V 0..5 server verbosity level. Default: 4"));

  /* copy values from CLI library to iodine */
  if (fio_cli_get("-V")) {
    int level = fio_cli_get_i("-V");
    if (level > 0 && level < 100)
      FIO_LOG_LEVEL = level;
  }

  if (fio_cli_get("-w")) {
    iodine_workers_set(IodineModule, INT2NUM(fio_cli_get_i("-w")));
  }
  if (fio_cli_get("-t")) {
    iodine_threads_set(IodineModule, INT2NUM(fio_cli_get_i("-t")));
  }
  if (fio_cli_get_bool("-v")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("log")), Qtrue);
  }
  if (fio_cli_get_bool("-warmup")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("warmup_")), Qtrue);
  }
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
    }
    rb_hash_aset(defaults, ID2SYM(rb_intern("address")),
                 rb_str_new_cstr(fio_cli_get("-b")));
  }
  if (fio_cli_get("-p")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("port")),
                 rb_str_new_cstr(fio_cli_get("-p")));
  }
  if (fio_cli_get("-www")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("public")),
                 rb_str_new_cstr(fio_cli_get("-www")));
  }
  if (!fio_cli_get("-redis") && getenv("IODINE_REDIS_URL")) {
    fio_cli_set("-redis", getenv("IODINE_REDIS_URL"));
  }
  if (fio_cli_get("-redis")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("redis_")),
                 rb_str_new_cstr(fio_cli_get("-redis")));
  }
  if (fio_cli_get("-k")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("timeout")),
                 INT2NUM(fio_cli_get_i("-k")));
  }
  if (fio_cli_get("-ping")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("ping")),
                 INT2NUM(fio_cli_get_i("-ping")));
  }
  if (fio_cli_get("-redis-ping")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("redis_ping_")),
                 INT2NUM(fio_cli_get_i("-redis-ping")));
  }
  if (fio_cli_get("-max-body")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("max_body")),
                 INT2NUM((fio_cli_get_i("-max-body") * 1024 * 1024)));
  }
  if (fio_cli_get("-max-message")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("max_msg")),
                 INT2NUM((fio_cli_get_i("-max-message") * 1024)));
  }
  if (fio_cli_get("-max-headers")) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("max_headers")),
                 INT2NUM((fio_cli_get_i("-max-headers") * 1024)));
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
                       fio_cli_get("-tls-key"), fio_cli_get("-tls-password"));
    } else {
      if (!fio_cli_get_bool("-tls"))
        FIO_LOG_ERROR("TLS support requires both key and certificate."
                      "\r\n\t\tfalling back on a self signed certificate.");
      char name[1024];
      fio_local_addr(name, 1024);
      fio_tls_cert_add(tls, name, NULL, NULL, NULL);
    }
    rb_hash_aset(defaults, iodine_tls_sym, rbtls);
  }
  if (fio_cli_unnamed_count()) {
    rb_hash_aset(defaults, ID2SYM(rb_intern("filename_")),
                 rb_str_new_cstr(fio_cli_unnamed(0)));
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
Ruby loads the library and invokes the Init_<lib_name> function...

Here we connect all the C code to the Ruby interface, completing the bridge
between Lib-Server and Ruby.
***************************************************************************** */
void Init_iodine(void) {
  // load any environment specific patches
  patch_env();

  // force the GVL state for the main thread
  IodineCaller.set_GVL(1);

  // Create the Iodine module (namespace)
  IodineModule = rb_define_module("Iodine");
  IodineBaseModule = rb_define_module_under(IodineModule, "Base");
  VALUE IodineCLIModule = rb_define_module_under(IodineBaseModule, "CLI");
  call_id = rb_intern2("call", 4);

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

  // register CLI methods
  rb_define_module_function(IodineCLIModule, "parse", iodine_cli_parse, 0);

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
