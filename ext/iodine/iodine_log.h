#ifndef H___IODINE_LOG___H
#define H___IODINE_LOG___H
#include "iodine.h"

/* *****************************************************************************
Iodine::Logger - Ruby-facing leveled logging

Routes Ruby log messages through the facil.io C STL logging system
(FIO_LOG_*), so Ruby output shares the same stderr sink, level prefixes,
truncation limits, and the global log level (Iodine::Logger.level /
FIO_LOG_LEVEL) as the C core.

Log levels (from facil.io):
- 0: FIO_LOG_LEVEL_NONE    - No logging
- 1: FIO_LOG_LEVEL_FATAL   - Fatal errors only
- 2: FIO_LOG_LEVEL_ERROR   - Errors and above
- 3: FIO_LOG_LEVEL_WARNING - Warnings and above
- 4: FIO_LOG_LEVEL_INFO    - Info and above (default)
- 5: FIO_LOG_LEVEL_DEBUG   - Debug and above

Ruby API:
- Iodine::Logger.fatal(*msg, &blk) / .error / .warn / .info / .debug
  (each argument logged separately; callable arguments invoked; a block's
  return value logged; nothing evaluated while the level is silenced)
- Iodine::Logger << msg (alias for info)
- Iodine::Logger.level / .level= (Integer 0-5 or Symbol)
- Iodine::Logger::NONE / FATAL / ERROR / WARN / INFO / DEBUG constants
***************************************************************************** */

static VALUE iodine_rb_IODINE_LOGGER;

/* *****************************************************************************
Leveled logging methods
***************************************************************************** */

/** Emits a single message at the given level through the C STL logging. */
static void iodine_log_emit_value(int level, VALUE msg) {
  VALUE str = rb_obj_as_string(msg);
  const char *cstr = StringValueCStr(str);
  switch (level) {
  case FIO_LOG_LEVEL_FATAL: FIO_LOG_FATAL("%s", cstr); break;
  case FIO_LOG_LEVEL_ERROR: FIO_LOG_ERROR("%s", cstr); break;
  case FIO_LOG_LEVEL_WARNING: FIO_LOG_WARNING("%s", cstr); break;
  case FIO_LOG_LEVEL_INFO: FIO_LOG_INFO("%s", cstr); break;
  default: FIO_LOG_DEBUG2("%s", cstr); break;
  }
}

/**
 * Shared implementation for all leveled methods.
 *
 * Each argument is logged as its own message. Arguments that answer `call`
 * (e.g. Proc / lambda) are invoked and their return value is logged. If a
 * block is given, it is called last and its return value is logged (Rack
 * SPEC compatible logger behavior).
 *
 * Lazy evaluation: when the level exceeds the current verbosity, nothing is
 * evaluated - arguments are not converted, callables are not invoked and the
 * block is not called.
 */
static VALUE iodine_log_write(int level, int argc, VALUE *argv) {
  if (level > FIO_LOG_LEVEL_GET())
    return Qnil;
  for (int i = 0; i < argc; ++i) {
    VALUE msg = argv[i];
    if (rb_respond_to(msg, IODINE_CALL_ID))
      msg = rb_funcall2(msg, IODINE_CALL_ID, 0, NULL);
    iodine_log_emit_value(level, msg);
  }
  if (rb_block_given_p())
    iodine_log_emit_value(
        level,
        rb_funcall2(rb_block_proc(), IODINE_CALL_ID, 0, NULL));
  return Qnil;
}

/**
 * @!method fatal(*messages, &block)
 * Logs each message at fatal level (level 1).
 *
 * Callable arguments are invoked and a block's return value is logged
 * (evaluated only when the level is active).
 *
 * @param messages [Array<Object>] the messages (converted with #to_s)
 * @return [nil]
 */
static VALUE iodine_log_fatal(int argc, VALUE *argv, VALUE self) {
  return iodine_log_write(FIO_LOG_LEVEL_FATAL, argc, argv);
  (void)self;
}

/**
 * @!method error(*messages, &block)
 * Logs each message at error level (level 2).
 *
 * Callable arguments are invoked and a block's return value is logged
 * (evaluated only when the level is active).
 *
 * @param messages [Array<Object>] the messages (converted with #to_s)
 * @return [nil]
 */
static VALUE iodine_log_error(int argc, VALUE *argv, VALUE self) {
  return iodine_log_write(FIO_LOG_LEVEL_ERROR, argc, argv);
  (void)self;
}

/**
 * @!method warn(*messages, &block)
 * Logs each message at warning level (level 3).
 *
 * Callable arguments are invoked and a block's return value is logged
 * (evaluated only when the level is active).
 *
 * @param messages [Array<Object>] the messages (converted with #to_s)
 * @return [nil]
 */
static VALUE iodine_log_warn(int argc, VALUE *argv, VALUE self) {
  return iodine_log_write(FIO_LOG_LEVEL_WARNING, argc, argv);
  (void)self;
}

/**
 * @!method info(*messages, &block)
 * Logs each message at info level (level 4).
 *
 * Callable arguments are invoked and a block's return value is logged
 * (evaluated only when the level is active).
 *
 * @param messages [Array<Object>] the messages (converted with #to_s)
 * @return [nil]
 */
static VALUE iodine_log_info(int argc, VALUE *argv, VALUE self) {
  return iodine_log_write(FIO_LOG_LEVEL_INFO, argc, argv);
  (void)self;
}

/**
 * @!method <<(message)
 * Alias for {#info} - logs the message at info level (level 4).
 *
 * @param message [Object] the message (converted with #to_s)
 * @return [nil]
 */
static VALUE iodine_log_lshift(int argc, VALUE *argv, VALUE self) {
  return iodine_log_write(FIO_LOG_LEVEL_INFO, argc, argv);
  (void)self;
}

/**
 * @!method debug(*messages, &block)
 * Logs each message at debug level (level 5), routed through FIO_LOG_DEBUG2
 * (the C file:line prefix is omitted, since it would point at the binding
 * rather than the Ruby caller).
 *
 * Callable arguments are invoked and a block's return value is logged
 * (evaluated only when the level is active).
 *
 * @param messages [Array<Object>] the messages (converted with #to_s)
 * @return [nil]
 */
static VALUE iodine_log_debug(int argc, VALUE *argv, VALUE self) {
  return iodine_log_write(FIO_LOG_LEVEL_DEBUG, argc, argv);
  (void)self;
}

/* *****************************************************************************
Level helpers
***************************************************************************** */

/**
 * @!method level
 * Returns the current logging level (the process-wide C STL log level).
 *
 * @return [Integer] the current log level (0-5)
 */
static VALUE iodine_log_level(VALUE self) {
  return RB_INT2FIX(((long)FIO_LOG_LEVEL_GET()));
  (void)self;
}

/**
 * @!method level=(level)
 * Sets the current logging level (the process-wide C STL log level).
 *
 * @param level [Integer, Symbol] the log level: 0-5, or one of
 *              :none, :fatal, :error, :warn, :info, :debug
 * @return [Integer, Symbol] the given level
 * @raise [TypeError] if level is not an Integer or Symbol
 * @raise [ArgumentError] if the level value is unknown or out of range
 */
static VALUE iodine_log_level_set(VALUE self, VALUE level) {
  int n;
  if (RB_TYPE_P(level, RUBY_T_SYMBOL)) {
    ID id = rb_sym2id(level);
    if (id == rb_intern("none")) {
      n = FIO_LOG_LEVEL_NONE;
    } else if (id == rb_intern("fatal")) {
      n = FIO_LOG_LEVEL_FATAL;
    } else if (id == rb_intern("error")) {
      n = FIO_LOG_LEVEL_ERROR;
    } else if (id == rb_intern("warn") || id == rb_intern("warning")) {
      n = FIO_LOG_LEVEL_WARNING;
    } else if (id == rb_intern("info")) {
      n = FIO_LOG_LEVEL_INFO;
    } else if (id == rb_intern("debug")) {
      n = FIO_LOG_LEVEL_DEBUG;
    } else {
      rb_raise(rb_eArgError,
               "unknown log level: :%s (expected :none, :fatal, :error, "
               ":warn, :info or :debug)",
               rb_id2name(id));
      return Qnil;
    }
  } else {
    rb_check_type(level, RUBY_T_FIXNUM);
    long lng = RB_FIX2INT(level);
    if (lng < FIO_LOG_LEVEL_NONE || lng > FIO_LOG_LEVEL_DEBUG) {
      rb_raise(rb_eArgError, "log level must be between 0 and 5");
      return Qnil;
    }
    n = (int)lng;
  }
  FIO_LOG_LEVEL_SET(n);
  return level;
  (void)self;
}

/* *****************************************************************************
Initialization
***************************************************************************** */

/**
 * Initializes the Iodine::Logger module.
 */
static void Init_Iodine_Logger(void) {
  iodine_rb_IODINE_LOGGER = rb_define_module_under(iodine_rb_IODINE, "Logger");
  STORE.hold(iodine_rb_IODINE_LOGGER);

  /* Log level constants (mirroring the facil.io FIO_LOG_LEVEL_* values) */
  rb_define_const(iodine_rb_IODINE_LOGGER,
                  "NONE",
                  RB_INT2FIX(FIO_LOG_LEVEL_NONE));
  rb_define_const(iodine_rb_IODINE_LOGGER,
                  "FATAL",
                  RB_INT2FIX(FIO_LOG_LEVEL_FATAL));
  rb_define_const(iodine_rb_IODINE_LOGGER,
                  "ERROR",
                  RB_INT2FIX(FIO_LOG_LEVEL_ERROR));
  rb_define_const(iodine_rb_IODINE_LOGGER,
                  "WARN",
                  RB_INT2FIX(FIO_LOG_LEVEL_WARNING));
  rb_define_const(iodine_rb_IODINE_LOGGER,
                  "INFO",
                  RB_INT2FIX(FIO_LOG_LEVEL_INFO));
  rb_define_const(iodine_rb_IODINE_LOGGER,
                  "DEBUG",
                  RB_INT2FIX(FIO_LOG_LEVEL_DEBUG));

  /* Leveled logging methods (module functions: callable on the module and
   * available as private instance methods when included) */
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "fatal",
                            iodine_log_fatal,
                            -1);
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "error",
                            iodine_log_error,
                            -1);
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "warn",
                            iodine_log_warn,
                            -1);
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "info",
                            iodine_log_info,
                            -1);
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "debug",
                            iodine_log_debug,
                            -1);
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "<<",
                            iodine_log_lshift,
                            -1);

  /* Level helpers */
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "level",
                            iodine_log_level,
                            0);
  rb_define_module_function(iodine_rb_IODINE_LOGGER,
                            "level=",
                            iodine_log_level_set,
                            1);
}

#endif /* H___IODINE_LOG___H */
