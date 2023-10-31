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
  } else {
    n = RB_INT2FIX(at);
    ++at;
  }
  STORE.hold(n);
  rb_hash_aset(h, n, v);
  tmp = rb_str_intern(n);
  STORE.release(n);
  n = tmp;
  STORE.hold(n);
  rb_hash_aset(h, n, v);
  STORE.release(n);
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
  long len = 0;
  char *argv[IODINE_CLI_LIMIT];
  if (rb_argv0 && RB_TYPE_P(rb_argv0, RUBY_T_STRING)) {
    argv[len++] = fio_bstr_write(NULL,
                                 RSTRING_PTR(rb_argv0),
                                 (size_t)RSTRING_LEN(rb_argv0));
  }
  for (long i = 0; len < IODINE_CLI_LIMIT && i < rb_array_len(rb_argv); ++i) {
    VALUE o = rb_ary_entry(rb_argv, i);
    if (!RB_TYPE_P(o, RUBY_T_STRING)) {
      FIO_LOG_WARNING("ARGV member skipped - not a String!");
      continue;
    }
    argv[len] = fio_bstr_write(NULL, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    fio_state_callback_add(FIO_CALL_AT_EXIT,
                           (void (*)(void *))fio_bstr_free,
                           (void *)(argv[len]));
    ++len;
  }

  VALUE iodine_version = rb_const_get(iodine_rb_IODINE, rb_intern("VERSION"));
  char *desc = fio_bstr_write2(
      NULL,
      FIO_STRING_WRITE_STR1("Iodine's (" FIO_POLL_ENGINE_STR
                            ") HTTP/WebSocket server version "),
      FIO_STRING_WRITE_STR2(RSTRING_PTR(iodine_version),
                            RSTRING_LEN(iodine_version)),
      FIO_STRING_WRITE_STR1(
          "\r\n\r\nUse:\r\n    iodine <options> <filename>\r\n\r\n"
          "Both <options> and <filename> are optional. i.e.,:\r\n"
          "    iodine -p 0 -b /tmp/my_unix_sock\r\n"
          "    iodine -p 8080 path/to/app/conf.ru\r\n"
          "    iodine -p 8080 -w 4 -t 16\r\n"
          "    iodine -w -1 -t 4 -r redis://usr:pass@localhost:6379/"));
  fio_state_callback_add(FIO_CALL_AT_EXIT,
                         (void (*)(void *))fio_bstr_free,
                         (void *)desc);

  fio_cli_end();
  fio_cli_start(
      len,
      (const char **)argv,
      0,
      ((required == Qnil || required == Qfalse) ? -1 : 1),
      desc,
      FIO_CLI_PRINT_HEADER("Address Binding"),
      FIO_CLI_PRINT_LINE(
          "NOTE: also controlled by the ADDRESS or PORT environment vars."),
      FIO_CLI_STRING("-bind -b address to listen to in URL format."),
      FIO_CLI_INT("-port -p port number to listen to if URL is missing."),
      FIO_CLI_PRINT(
          "Note: these are optional and supersede previous instructions."),

      FIO_CLI_PRINT_HEADER("Concurrency"),
      FIO_CLI_INT("--threads (-4) -t number of worker threads to use."),
      FIO_CLI_INT("--workers (-2) -w number of worker processes to use."),

      FIO_CLI_PRINT_HEADER("HTTP"),
      FIO_CLI_STRING("--public -www public folder for static file service."),
      FIO_CLI_INT("--max-line -maxln per-header line limit, in Kb."),
      FIO_CLI_INT("--max-header -maxhd total header limit per request, in Kb."),
      FIO_CLI_INT(
          "--max-body -maxbd total message payload limit per request, in Mb."),
      FIO_CLI_INT("--keep-alive -k (" FIO_MACRO2STR(
          FIO_HTTP_DEFAULT_TIMEOUT) ") HTTP keep-alive timeout in seconds "
                                    "(0..255)"),
      FIO_CLI_BOOL("--log -v log HTTP messages."),

      FIO_CLI_PRINT_HEADER("WebSocket / SSE"),
      FIO_CLI_INT(
          "--ws-max-msg -maxms incoming WebSocket message limit, in Kb."),
      FIO_CLI_INT("--timeout -ping WebSocket / SSE timeout, in seconds."),

      FIO_CLI_PRINT_HEADER("TLS / SSL"),
      FIO_CLI_PRINT_LINE(
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
      FIO_CLI_STRING("--config -C configuration file to be loaded."),
      FIO_CLI_STRING(
          "--pid -pidfile -pid name for the pid file to be created."),
      FIO_CLI_BOOL(
          "--preload -warmup warm up the application. CAREFUL! with workers."),
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

  /* Test for TLS */
  fio_tls_s *tls = (fio_cli_get("--tls-cert") && fio_cli_get("--tls-key"))
                       ? fio_tls_cert_add(fio_tls_new(),
                                          fio_cli_get("--tls-name"),
                                          fio_cli_get("--tls-cert"),
                                          fio_cli_get("--tls-key"),
                                          fio_cli_get("-tls-pass"))
                   : fio_cli_get("-tls")
                       ? fio_tls_cert_add(fio_tls_new(),
                                          fio_cli_get("-tls-name"),
                                          NULL,
                                          NULL,
                                          NULL)
                       : NULL;
  /* support -b and -p for when a URL isn't provided */
  if (fio_cli_get("-b"))
    fio_cli_set_unnamed(0, fio_cli_get("-b"));
  if (fio_cli_get("-p")) {
    fio_buf_info_s tmp;
    FIO_STR_INFO_TMP_VAR(url, 2048);
    tmp.buf = (char *)fio_cli_unnamed(0);
    if (!tmp.buf)
      tmp.buf = (char *)"0.0.0.0";
    tmp.len = strlen(tmp.buf);
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
    fio_cli_set_unnamed(0, url.buf);
  }

  /* Save data to Hash and return it... why? I don't know. */
  VALUE h = rb_hash_new();
  STORE.hold(h);
  fio_cli_each(iodine_cli_task, (void *)h);
  /* cleanup */
  fio_state_callback_remove(FIO_CALL_AT_EXIT,
                            (void (*)(void *))fio_bstr_free,
                            (void *)desc);
  fio_bstr_free(desc);
  for (long i = 0; i < len; ++i) {
    fio_state_callback_remove(FIO_CALL_AT_EXIT,
                              (void (*)(void *))fio_bstr_free,
                              (void *)(argv[i]));
    fio_bstr_free(argv[i]);
  }
  STORE.release(h);
  return h;
}

static VALUE iodine_cli_get(VALUE self, VALUE key) {
  VALUE r = Qnil;
  fio_cli_arg_e t = FIO_CLI_ARG_NONE;
  const char *val = NULL;
  if (RB_TYPE_P(key, RUBY_T_FIXNUM)) {
    val = fio_cli_unnamed(NUM2UINT(key));
    goto finish_string;
  }
  if (RB_TYPE_P(key, RUBY_T_SYMBOL))
    key = rb_sym2str(key);
  if (!RB_TYPE_P(key, RUBY_T_STRING))
    rb_raise(rb_eArgError,
             "key should be either an Integer, a String or a Symbol");
  t = fio_cli_type(RSTRING_PTR(key));
  if (t == FIO_CLI_ARG_INT || t == FIO_CLI_ARG_STRING)
    r = LL2NUM(fio_cli_get_i(RSTRING_PTR(key)));
  else
    val = fio_cli_get(RSTRING_PTR(key));

finish_string:
  if (val)
    r = rb_str_new(val, strlen(val));
  return r;
}

static VALUE iodine_cli_set(VALUE self, VALUE key, VALUE value) {
  if (fio_srv_is_running() || !fio_srv_is_master())
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

/* *****************************************************************************
Initialize CLI API
***************************************************************************** */
static void Init_iodine_cli(void) { // clang-format on
  /** The Iodine::Base module is for internal concerns. */
  VALUE cli = rb_define_module_under(iodine_rb_IODINE_BASE, "CLI");
  rb_define_singleton_method(cli, "parse", iodine_cli_parse, 1);
  rb_define_singleton_method(cli, "[]", iodine_cli_get, 1);
  rb_define_singleton_method(cli, "[]=", iodine_cli_set, 2);
  iodine_cli_parse(cli, Qfalse);
}

/* *****************************************************************************

Iodine's HTTP/WebSocket server version 0.7.56

Use:
    iodine <options> <filename>

Both <options> and <filename> are optional. i.e.,:
    iodine -p 0 -b /tmp/my_unix_sock
    iodine -p 8080 path/to/app/conf.ru
    iodine -p 8080 -w 4 -t 16
    iodine -w -1 -t 4 -r redis://usr:pass@localhost:6379/

***************************************************************************** */
