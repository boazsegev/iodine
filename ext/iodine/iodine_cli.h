#ifndef H___IODINE_CLI___H
#define H___IODINE_CLI___H
#include "iodine.h"

#define IODINE_CLI_LIMIT 256

static int iodine_cli_task(fio_buf_info_s name,
                           fio_buf_info_s val,
                           fio_cli_arg_e t,
                           void *h_) {
  static long at = 0;
  VALUE h = (VALUE)h_;
  VALUE tmp = Qnil;
  VALUE n = Qnil;
  VALUE v = Qnil;
  switch (t) {
  case FIO_CLI_ARG_BOOL: v = Qtrue; break;
  case FIO_CLI_ARG_INT: v = RB_LL2NUM(fio_atol(&val.buf)); break;
  default: v = rb_str_new(val.buf, val.len); break;
  }
  STORE.hold(v);
  if (name.buf) {
    at = 0;
    while (name.buf[0] == '-') {
      ++name.buf;
      --name.len;
    }
    n = rb_str_new(name.buf, name.len);
    STORE.hold(n);
  } else {
    n = RB_INT2FIX(at);
    ++at;
  }
  rb_hash_aset(h, n, v);
  if (RB_TYPE_P(n, RUBY_T_STRING)) {
    tmp = rb_str_intern(n);
    STORE.release(n);
    STORE.hold((n = tmp));
    rb_hash_aset(h, n, v);
    STORE.release(n);
  }
  STORE.release(v);
  return 0;
}

/* *****************************************************************************
Ruby Public API.
***************************************************************************** */

/** Read CLI as required data. */
static VALUE iodine_cli_parse(VALUE self, VALUE required) {
  if (!RB_TYPE_P(rb_argv, RUBY_T_ARRAY))
    rb_raise(rb_eException, "ARGV should be an Array!");
  FIO_STR_INFO_TMP_VAR(desc, 2048);
  FIO_STR_INFO_TMP_VAR(threads, 128);
  FIO_STR_INFO_TMP_VAR(workers, 128);
  const char *argv[IODINE_CLI_LIMIT];
  const char *threads_env = NULL;
  const char *workers_env = NULL;
  long len = 0;
  VALUE iodine_version = rb_const_get(iodine_rb_IODINE, rb_intern("VERSION"));

  /* I don't know if Ruby promises a NUL separator, but fio_bstr does. */
  if (rb_argv0 && RB_TYPE_P(rb_argv0, RUBY_T_STRING)) {
    argv[len++] =
        fio_bstr_write(NULL, RSTRING_PTR(rb_argv0), RSTRING_LEN(rb_argv0));
  } else
    argv[len++] = (const char *)"iodine";
  for (long i = 0; len < IODINE_CLI_LIMIT && i < rb_array_len(rb_argv); ++i) {
    VALUE o = rb_ary_entry(rb_argv, i);
    if (!RB_TYPE_P(o, RUBY_T_STRING)) {
      FIO_LOG_WARNING("ARGV member skipped - not a String!");
      continue;
    }
    argv[len++] = fio_bstr_write(NULL, RSTRING_PTR(o), RSTRING_LEN(o));
  }

  /* in case of `-h` or error, make sure we clean up */
  for (long i = 0; i < len; ++i)
    fio_state_callback_add(FIO_CALL_AT_EXIT,
                           (void (*)(void *))fio_bstr_free,
                           (void *)argv[i]);

  threads_env = getenv("THREADS");
  workers_env = getenv("WORKERS");

  fio_string_write2(
      &threads,
      NULL,
      FIO_STRING_WRITE_STR1("--threads -t ("),
      (threads_env ? FIO_STRING_WRITE_STR1(threads_env)
                   : FIO_STRING_WRITE_STR1("-4")),
      FIO_STRING_WRITE_STR1(") number of worker threads to use."));
  fio_string_write2(
      &workers,
      NULL,
      FIO_STRING_WRITE_STR1("--workers -w ("),
      (workers_env ? FIO_STRING_WRITE_STR1(workers_env)
                   : FIO_STRING_WRITE_STR1("-2")),
      FIO_STRING_WRITE_STR1(") number of worker processes to use."));

  fio_string_write2(
      &desc,
      NULL,
      FIO_STRING_WRITE_STR1("Iodine's (" FIO_POLL_ENGINE_STR
                            ") HTTP/WebSocket server version "),
      FIO_STRING_WRITE_STR2(RSTRING_PTR(iodine_version),
                            (size_t)RSTRING_LEN(iodine_version)),
      FIO_STRING_WRITE_STR1(
          "\r\n\r\nUse:\r\n    iodine <options> <filename>\r\n\r\n"
          "Both <options> and <filename> are optional. i.e.,:\r\n"
          "    iodine -p 0 -b /tmp/my_unix_sock\r\n"
          "    iodine -p 8080 path/to/app/conf.ru\r\n"
          "    iodine -p 8080 -w 4 -t 16\r\n"
          "    iodine -w -1 -t 4 -r redis://usr:pass@localhost:6379/"));

  fio_cli_end();
  fio_cli_start(
      (int)len,
      (const char **)argv,
      0,
      ((required == Qnil || required == Qfalse) ? -1 : 1),
      desc.buf,
      FIO_CLI_PRINT_HEADER("Address Binding"),
      FIO_CLI_PRINT_LINE(
          "NOTE: also controlled by the ADDRESS or PORT environment vars."),
      FIO_CLI_STRING(
          "-bind -b address to listen to in URL format (MAY include PORT)."),
      FIO_CLI_PRINT(
          "It's possible to add TLS/SSL data to the binding URL. i.e.:"),
      FIO_CLI_PRINT("\t iodine -b https://0.0.0.0/tls=./cert_path/"),
      FIO_CLI_PRINT(
          "\t iodine -b https://0.0.0.0/key=./key.pem&cert=./cert.pem"),
      FIO_CLI_INT("-port -p default port number to listen to."),
      FIO_CLI_PRINT(
          "Note: these are optional and supersede previous instructions."),

      FIO_CLI_PRINT_HEADER("Concurrency"),
      FIO_CLI_INT(threads.buf),
      FIO_CLI_INT(workers.buf),

      FIO_CLI_PRINT_HEADER("HTTP"),
      FIO_CLI_STRING("--public -www public folder for static file service."),
      FIO_CLI_INT("--max-line -maxln (" FIO_MACRO2STR(
          FIO_HTTP_DEFAULT_MAX_LINE_LEN) ") per-header line limit, in bytes."),
      FIO_CLI_INT("--max-header -maxhd (" FIO_MACRO2STR(
          FIO_HTTP_DEFAULT_MAX_HEADER_SIZE) ") total header limit per request, "
                                            "in bytes."),
      FIO_CLI_INT("--max-body -maxbd (" FIO_MACRO2STR(
          FIO_HTTP_DEFAULT_MAX_BODY_SIZE) ") total message payload limit per "
                                          "request, in bytes."),
      FIO_CLI_INT("--keep-alive -k (" FIO_MACRO2STR(
          FIO_HTTP_DEFAULT_TIMEOUT) ") HTTP keep-alive timeout in seconds "
                                    "(0..255)"),
      FIO_CLI_INT("--max-age -maxage (3600) default Max-Age header value for "
                  "static files."),
      FIO_CLI_BOOL("--log -v log HTTP messages."),

      FIO_CLI_PRINT_HEADER("WebSocket / SSE"),
      FIO_CLI_INT("--ws-max-msg -maxms (" FIO_MACRO2STR(
          FIO_HTTP_DEFAULT_WS_MAX_MSG_SIZE) ") incoming WebSocket message "
                                            "limit, in bytes."),
      FIO_CLI_INT("--timeout -ping (" FIO_MACRO2STR(
          FIO_HTTP_DEFAULT_TIMEOUT_LONG) ") WebSocket / SSE timeout, in "
                                         "seconds."),

      FIO_CLI_PRINT_HEADER("TLS / SSL"),
      FIO_CLI_PRINT(
          "NOTE: crashes if no crypto library implementation is found."),
      FIO_CLI_BOOL(
          "--tls-self -tls uses SSL/TLS with a self signed certificate."),
      FIO_CLI_STRING("--tls-name -name The host name for the SSL/TLS "
                     "certificate (if any)."),
      FIO_CLI_STRING("--tls-cert -cert The SSL/TLS certificate .pem file."),
      FIO_CLI_STRING("--tls-key -key The SSL/TLS private key .pem file."),
      FIO_CLI_STRING(
          "--tls-password -tls-pass The SSL/TLS password for the private key."),

      FIO_CLI_PRINT_HEADER("Clustering Pub/Sub"),
      FIO_CLI_INT("--broadcast -bp Cluster Broadcast Port."),
      FIO_CLI_STRING("--secret -scrt Cluster Secret."),
      FIO_CLI_PRINT("NOTE: also controlled by the SECRET and SECRET_LENGTH "
                    "environment vars."),

      FIO_CLI_PRINT_HEADER("Connecting Iodine to Redis:"),
      FIO_CLI_STRING(
          "--redis -r an optional Redis URL server address. Default: none."),
      FIO_CLI_INT("--redis-ping -rp Redis ping interval in seconds."),

      FIO_CLI_PRINT_HEADER("Misc"),
      FIO_CLI_BOOL("--verbose -V -d print out debugging messages."),
      FIO_CLI_BOOL("--rack -R -rack prefer Rack::Builder over NeoRack."),
      FIO_CLI_STRING("--config -C configuration file to be loaded."),
      FIO_CLI_STRING(
          "--pid -pidfile -pid name for the pid file to be created."),
      FIO_CLI_BOOL(/* TODO! fixme? shouldn't preload be the default? */
                   "--preload -warmup warm up the application. CAREFUL! with "
                   "workers."),
      FIO_CLI_BOOL(
          "--contained attempts to handle possible container restrictions."),
      FIO_CLI_PRINT(
          "Containers sometimes impose file-system restrictions, i.e.,"),
      FIO_CLI_PRINT("the IPC Unix Socket might need to be placed in `/tmp`."));

  /* review CLI for logging */
  if (fio_cli_get_bool("-V")) {
    FIO_LOG_LEVEL = FIO_LOG_LEVEL_DEBUG;
  }

  if (fio_cli_get_bool("--contained")) { /* container - IPC url in tmp */
    char *u = (char *)fio_pubsub_ipc_url();
    memcpy((void *)(u + 7), "/tmp/", 5);
  }

  /* Clustering */
  if (fio_cli_get_i("-bp") > 0) {
    fio_buf_info_s scrt = FIO_BUF_INFO1((char *)fio_cli_get("-scrt"));
    fio_pubsub_secret_set(scrt.buf, scrt.len);
    fio_pubsub_broadcast_on_port(fio_cli_get_i("-bp"));
  }

  /* support -b and -p for when a URL isn't provided */
  if (fio_cli_get("-p")) {
    fio_buf_info_s tmp = fio_cli_get_str("-b");
    if (tmp.buf) {
      FIO_STR_INFO_TMP_VAR(url, 2048);
      FIO_ASSERT(tmp.len < 2000, "binding address / url too long.");
      fio_url_s u = fio_url_parse(tmp.buf, tmp.len);
      tmp.buf = (char *)fio_cli_get("-p");
      tmp.len = strlen(tmp.buf);
      FIO_ASSERT(tmp.len < 6, "port number too long.");
      fio_string_write2(&url,
                        NULL,
                        FIO_STRING_WRITE_STR2(u.scheme.buf, u.scheme.len),
                        (u.scheme.len ? FIO_STRING_WRITE_STR2("://", 3)
                                      : FIO_STRING_WRITE_STR2(NULL, 0)),
                        FIO_STRING_WRITE_STR2(u.host.buf, u.host.len),
                        FIO_STRING_WRITE_STR2(":", 1),
                        FIO_STRING_WRITE_STR2(tmp.buf, tmp.len),
                        (u.query.len ? FIO_STRING_WRITE_STR2("?", 1)
                                     : FIO_STRING_WRITE_STR2(NULL, 0)),
                        FIO_STRING_WRITE_STR2(u.query.buf, u.query.len));
      fio_cli_set("-b", url.buf);
    } else {
#if FIO_OS_WIN
      SetEnvironmentVariable("PORT", fio_cli_get("-p"));
#else
      setenv("PORT", fio_cli_get("-p"), 1);
#endif
    }
  }

  /* Save data to Hash and return it... why? I don't know. */
  VALUE h = rb_hash_new();
  STORE.hold(h);
  fio_cli_each(iodine_cli_task, (void *)h);
  /* cleanup */
  for (long i = 0; i < len; ++i) {
    fio_state_callback_remove(FIO_CALL_AT_EXIT,
                              (void (*)(void *))fio_bstr_free,
                              (void *)argv[i]);
    fio_bstr_free((char *)argv[i]);
  }
  STORE.release(h);
  return h;
}

static VALUE iodine_cli_get(VALUE self, VALUE key) {
  VALUE r = Qnil;
  fio_buf_info_s val;
  int64_t ival = 0;
  char *tmp;
  if (RB_TYPE_P(key, RUBY_T_FIXNUM)) {
    val = fio_cli_unnamed_str(NUM2UINT(key));
    r = rb_str_new(val.buf, val.len);
    return r;
  }
  if (RB_TYPE_P(key, RUBY_T_SYMBOL))
    key = rb_sym2str(key);
  if (!RB_TYPE_P(key, RUBY_T_STRING))
    rb_raise(rb_eArgError,
             "key should be either an Integer, a String or a Symbol");
  val = fio_cli_get_str(RSTRING_PTR(key));
  if (!val.len)
    return r;
  tmp = val.buf;
  ival = fio_atol(&tmp);
  if (tmp == val.buf + val.len)
    r = LL2NUM(ival);
  else
    r = rb_str_new(val.buf, val.len);
  return r;
}

static VALUE iodine_cli_set(VALUE self, VALUE key, VALUE value) {
  if (fio_io_is_running() || !fio_io_is_master())
    rb_raise(rb_eException,
             "Setting CLI arguments can only be performed before Iodine.start "
             "and in the master process.");
  if (RB_TYPE_P(key, RUBY_T_FIXNUM)) {
    if (!RB_TYPE_P(value, RUBY_T_STRING))
      rb_raise(rb_eArgError,
               "value for an indexed CLI argument should be a String");
    fio_cli_set_unnamed(NUM2UINT(key), RSTRING_PTR(value));
    return value;
  }
  if (RB_TYPE_P(key, RUBY_T_SYMBOL))
    key = rb_sym2str(key);
  if (!RB_TYPE_P(key, RUBY_T_STRING))
    rb_raise(rb_eArgError,
             "key should be either an Integer, a String or a Symbol");
  fio_cli_arg_e t = fio_cli_type(RSTRING_PTR(key));
  if (t == FIO_CLI_ARG_INT || t == FIO_CLI_ARG_STRING) {
    if (!RB_TYPE_P(value, RUBY_T_FIXNUM))
      rb_raise(rb_eArgError,
               "value for %s should be an Integer",
               RSTRING_PTR(key));
    fio_cli_set_i(RSTRING_PTR(key), NUM2LL(value));
  } else {
    if (!RB_TYPE_P(value, RUBY_T_STRING))
      rb_raise(rb_eArgError,
               "value for %s should be a String",
               RSTRING_PTR(key));
    fio_cli_set(RSTRING_PTR(key), RSTRING_PTR(value));
  }
  return value;
}

/** Initialize Iodine::Base::CLI */
/**
 * The Iodine::Base::CLI module is used internally to manage CLI options.
 */
static void Init_Iodine_Base_CLI(void) {
  VALUE cli = rb_define_module_under(iodine_rb_IODINE_BASE, "CLI");
  rb_define_singleton_method(cli, "parse", iodine_cli_parse, 1);
  rb_define_singleton_method(cli, "[]", iodine_cli_get, 1);
  rb_define_singleton_method(cli, "[]=", iodine_cli_set, 2);
  iodine_cli_parse(cli, Qfalse);
}
#endif /* H___IODINE_CLI___H */
