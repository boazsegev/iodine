#include "iodine.h"
#include "iodine_tls.h"

/* *****************************************************************************
Ruby Connection Object
***************************************************************************** */

FIO_LEAK_COUNTER_DEF(iodine_connection)

typedef enum {
  IODINE_CONNECTION_STORE_handler,
  IODINE_CONNECTION_STORE_env,
  IODINE_CONNECTION_STORE_FINISH,
} iodine_connection_store_e;

typedef struct iodine_connection_s {
  fio_s *io;
  fio_http_s *http; /* handler should be an HTTP udata */
  VALUE store[IODINE_CONNECTION_STORE_FINISH];
} iodine_connection_s;

static void iodine_connection_free(void *ptr_) {
  iodine_connection_s *c = (iodine_connection_s *)ptr_;
  FIO_LEAK_COUNTER_ON_FREE(iodine_connection);
}

static void iodine_connection_mark(struct iodine_connection_s *c) {
  for (size_t i = 0; i < IODINE_CONNECTION_STORE_FINISH; ++i)
    if (!IODINE_STORE_IS_SKIP(c->store[i]))
      rb_gc_mark(c->store[i]);
}

static VALUE iodine_connection_alloc(VALUE klass) {
  iodine_connection_s *wrapper;
  VALUE o = Data_Make_Struct(klass,
                             iodine_connection_s,
                             iodine_connection_mark,
                             iodine_connection_free,
                             wrapper);
  *wrapper = (iodine_connection_s){NULL};
  FIO_LEAK_COUNTER_ON_ALLOC(iodine_connection);
  for (size_t i = 0; i < IODINE_CONNECTION_STORE_FINISH; ++i)
    wrapper->store[i] = Qnil;
  return o;
}

static iodine_connection_s *iodine_connection_get(VALUE self) {
  iodine_connection_s *c;
  Data_Get_Struct(self, iodine_connection_s, c);
  return c;
}

/* *****************************************************************************
Ruby Store Get/Set
***************************************************************************** */

#define IODINE_DEF_GET_FUNC(val_name)                                          \
  /** Returns the client's current val_name object. */                         \
  static VALUE iodine_connection_##val_name##_get(VALUE self) {                \
    iodine_connection_s *c = iodine_connection_get(self);                      \
    if (c)                                                                     \
      return c->store[IODINE_CONNECTION_STORE_##val_name];                     \
    return Qnil;                                                               \
  }
#define IODINE_DEF_SET_FUNC(val_name)                                          \
  /** Sets the client's val_name object. */                                    \
  static VALUE iodine_connection_##val_name##_set(VALUE self,                  \
                                                  VALUE updated_val) {         \
    iodine_connection_s *c = iodine_connection_get(self);                      \
    if (!c)                                                                    \
      return Qnil;                                                             \
    c->store[IODINE_CONNECTION_STORE_##val_name] = updated_val;                \
    return Qnil;                                                               \
  }

IODINE_DEF_GET_FUNC(handler);
IODINE_DEF_SET_FUNC(env);

#undef IODINE_DEF_GET_FUNC
#undef IODINE_DEF_SET_FUNC
/* *****************************************************************************
Default Handler Implementations TODO
***************************************************************************** */

static VALUE iodine_handler_deafult_on_event(VALUE handler, VALUE client) {
  return Qnil;
  (void)handler, (void)client;
}

static VALUE iodine_handler_deafult_on_data(VALUE handler, VALUE client) {
  const size_t max_read = IODINE_DEFAULT_ON_DATA_READ_BUFFER;
  iodine_connection_s *c = iodine_connection_get(client);
  if (!c || !c->io)
    return Qnil;
  VALUE buf = rb_str_buf_new(max_read);
  STORE.hold(buf);
  size_t actual = fio_read(c->io, RSTRING_PTR(buf), max_read);
  if (!actual)
    return Qnil;
  rb_str_set_len(buf, actual);
  VALUE args[] = {client, buf};
  rb_funcall(handler, IODINE_ON_MESSAGE_ID, 2, args);
  STORE.release(buf);
  return Qnil;
}

static VALUE iodine_handler_deafult_on_message(VALUE h, VALUE c, VALUE m) {
  return Qnil;
  (void)h, (void)c, (void)m;
}

static VALUE iodine_handler_deafult_on_http(VALUE handler, VALUE client) {
  /* wrap RACK specification here. */
  return rb_funcall(handler, IODINE_CALL_ID, 1, &client);
}

static VALUE iodine_handler_method_injection(VALUE handler) {
/* TODO - test for handler responses */
#define IODINE_DEFINE_MISSING_CALLBACK(id)                                     \
  if (!rb_respond_to(handler, id))                                             \
    rb_define_method(handler,                                                  \
                     rb_id2name(id),                                           \
                     iodine_handler_deafult_on_event,                          \
                     1);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_CALL_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_NEW_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_ATTACH_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_SSE_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_WEBSOCKET_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_CLOSE_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_EVENTSOURCE_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_EVENTSOURCE_RECONNECT_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_FINISH_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_OPEN_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_READY_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_SHUTDOWN_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_TIMEOUT_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_PRE_HTTP_BODY_ID);
#undef IODINE_DEFINE_MISSING_CALLBACK

  if (!rb_respond_to(handler, IODINE_ON_DATA_ID))
    rb_define_method(handler, "on_data", iodine_handler_deafult_on_data, 1);
  if (!rb_respond_to(handler, IODINE_ON_HTTP_ID))
    rb_define_method(handler, "on_http", iodine_handler_deafult_on_http, 1);
  if (!rb_respond_to(handler, IODINE_ON_MESSAGE_ID))
    rb_define_method(handler,
                     "on_message",
                     iodine_handler_deafult_on_message,
                     2);
  return handler;
}

/* *****************************************************************************
Handler Set function
***************************************************************************** */

/** Sets the client's val_name object. */
static VALUE iodine_connection_handler_set(VALUE client, VALUE handler) {
  iodine_connection_s *c = iodine_connection_get(client);
  if (!c)
    return Qnil;
  iodine_handler_method_injection(handler);
  c->store[IODINE_CONNECTION_STORE_handler] = handler;
  return handler;
}

/* *****************************************************************************
ENV get / conversion
***************************************************************************** */

static VALUE IODINE_CONNECTION_ENV_TEMPLATE = Qnil;

static int iodine_env_populate_header_data(fio_http_s *h,
                                           fio_str_info_s n,
                                           fio_str_info_s v,
                                           void *env_) {
  /* CONTENT_LENGTH and CONTENT_TYPE should be copied without HTTP_ */
  const uint64_t content_length1 = fio_buf2u64u("content_");
  const uint64_t content_length2 = fio_buf2u64u("t_length");
  const uint64_t content_type1 = fio_buf2u64u("content_");
  const uint32_t content_type2 = fio_buf2u32u("type");
  size_t offset =
      5 * !((n.len == 14 && fio_buf2u64u(n.buf) == content_length1 &&
             fio_buf2u64u(n.buf + 6) == content_length2) ||
            (n.len == 12 && fio_buf2u64u(n.buf) == content_type1 &&
             fio_buf2u32u(n.buf + 8) == content_type2));
  VALUE key = rb_str_buf_new(n.len + offset);
  STORE.hold(key);
  /* add HTTP_ (if required), copy as uppercase + replace '-'' with '_' */
  FIO_MEMCPY(RSTRING_PTR(key), "HTTP_", 5);
  char *rb_key_ptr = RSTRING_PTR(key) + offset;
  for (size_t i = 0; i < n.len; ++i) { /* copy Uppercase */
    rb_key_ptr[i] = n.buf[i] | ((n.buf[i] >= 'a' && n.buf[i] <= 'z') << 5);
    if (rb_key_ptr[i] == '-')
      rb_key_ptr[i] = '_';
  }
  rb_str_set_len(key, n.len + offset);
  /* copy value */
  VALUE val = rb_str_new(v.buf, v.len);
  STORE.hold(val);
  /* finish up */
  rb_hash_aset((VALUE)env_, key, val);
  STORE.release(val);
  STORE.release(key);
  (void)h;
}

static void iodine_env_set_key_pair(VALUE env,
                                    fio_str_info_s n,
                                    fio_str_info_s v) {
  VALUE key = rb_str_new(n.buf, n.len);
  STORE.hold(key);
  VALUE val = rb_str_new(v.buf, v.len);
  STORE.hold(val);
  rb_hash_aset(env, key, val);
  STORE.release(val);
  STORE.release(key);
}

static void iodine_env_set_const_val(VALUE env, fio_str_info_s n, VALUE val) {
  VALUE key = rb_str_new(n.buf, n.len);
  STORE.hold(key);
  rb_hash_aset(env, key, val);
  STORE.release(key);
}

static void iodine_connection_init_env_template(fio_buf_info_s at_url) {
  VALUE env = IODINE_CONNECTION_ENV_TEMPLATE = rb_hash_new();
  STORE.hold(IODINE_CONNECTION_ENV_TEMPLATE);
  /* TODO: set template, see https://github.com/rack/rack/blob/main/SPEC.rdoc */
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"REQUEST_METHOD"), Qtrue);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"PATH_INFO"), Qtrue);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"QUERY_STRING"), Qtrue);
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"SERVER_PROTOCOL"),
                           Qtrue);
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO1((char *)"SERVER_NAME"),
                          FIO_STR_INFO0);
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO1((char *)"SCRIPT_NAME"),
                          FIO_STR_INFO0);
  if (at_url.len) {
    fio_url_s u = fio_url_parse(at_url.buf, at_url.len);
    uint64_t port_num = u.port.len ? fio_atol(&u.port.buf) : 0;
    if (port_num && (port_num < 0xFFFF))
      iodine_env_set_const_val(env,
                               FIO_STR_INFO1((char *)"SERVER_PORT"),
                               UINT2NUM((unsigned)port_num));
  } else if (fio_cli_get_i("-p")) {
    iodine_env_set_const_val(env,
                             FIO_STR_INFO1((char *)"SERVER_PORT"),
                             UINT2NUM((unsigned)fio_cli_get_i("-p")));
  } else if (fio_cli_unnamed(0)) {
    fio_url_s u = fio_url_parse(fio_cli_unnamed(0), strlen(fio_cli_unnamed(0)));
    uint64_t port_num = u.port.len ? fio_atol(&u.port.buf) : 0;
    if (port_num && (port_num < 0xFFFF))
      iodine_env_set_const_val(env,
                               FIO_STR_INFO1((char *)"SERVER_PORT"),
                               UINT2NUM((unsigned)port_num));
  } else if (getenv("PORT") && strlen(getenv("PORT"))) {
    char *tmp = getenv("PORT");
    uint64_t port_num = fio_atol(&tmp);
    iodine_env_set_const_val(env,
                             FIO_STR_INFO1((char *)"SERVER_PORT"),
                             UINT2NUM((unsigned)port_num));
  } else {
    iodine_env_set_const_val(env,
                             FIO_STR_INFO1((char *)"SERVER_PORT"),
                             UINT2NUM((unsigned)3000));
  }

  iodine_env_set_key_pair(
      env,
      FIO_STR_INFO2((char *)"rack.url_scheme", 15),
      FIO_STR_INFO2((char *)"https",
                    (fio_cli_get("-tls") ? 5 : 4))); /* FIXME? */
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.input"),
                           Qnil); /* TODO! */
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.multithread"),
                           Qtrue);
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.multiprocess"),
                           Qtrue);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"rack.run_once"), Qfalse);
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.errors"),
                           rb_stderr);
  // rb_hash_aset(env, rb_str_new("rack.hijack?", 12), Qtrue);   /* TODO? */
  // rb_hash_aset(env, rb_str_new("rack.hijack", 11), Qnil);     /* self? */
}

/** Returns the client's current val_name object. */
static VALUE iodine_connection_env_get(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c || !c->http)
    return Qnil;
  if (c->store[IODINE_CONNECTION_STORE_env] != Qnil)
    return c->store[IODINE_CONNECTION_STORE_env];
  VALUE env = c->store[IODINE_CONNECTION_STORE_env] =
      rb_hash_dup(IODINE_CONNECTION_ENV_TEMPLATE);
  /* TODO! populate env, see https://github.com/rack/rack/blob/main/SPEC.rdoc */
  fio_http_request_header_each(c->http,
                               iodine_env_populate_header_data,
                               (void *)env);
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO2((char *)"REQUEST_METHOD", 14),
                          fio_keystr_info(&c->http->method));
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO2((char *)"PATH_INFO", 9),
                          fio_keystr_info(&c->http->path));
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO2((char *)"QUERY_STRING", 12),
                          fio_keystr_info(&c->http->query));
  iodine_env_set_key_pair(
      env,
      FIO_STR_INFO2((char *)"SERVER_NAME", 11),
      fio_http_request_header(c->http, FIO_STR_INFO2((char *)"host", 4), 0));
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO2((char *)"SERVER_PROTOCOL", 12),
                          fio_keystr_info(&c->http->version));
  return Qnil;
}

/* *****************************************************************************
Raw IO Callbacks and Helpers (TCP/IP)
***************************************************************************** */

#define IODINE_CONNECTION_DEF_CB(named, id)                                    \
  static void iodine_io_raw_##named(fio_s *io) {                               \
    VALUE handler = (VALUE)fio_udata_get(io);                                  \
    if (!handler || handler == Qnil)                                           \
      return;                                                                  \
    iodine_ruby_call_outside(handler, id, 0, NULL);                            \
  }

/** Called when an IO is attached to the protocol. */
IODINE_CONNECTION_DEF_CB(on_attach, IODINE_ON_ATTACH_ID);
/** Called when a data is available. */
IODINE_CONNECTION_DEF_CB(on_data, IODINE_ON_DATA_ID);
/** called once all pending `write` calls are finished. */
IODINE_CONNECTION_DEF_CB(on_ready, IODINE_ON_READY_ID);
/** called if the connection is open when the server is shutting down. */
IODINE_CONNECTION_DEF_CB(on_shutdown, IODINE_ON_SHUTDOWN_ID);
/** Called when a connection's open while server is shutting down. */
static void iodine_io_raw_on_shutdown(fio_s *io);
/** Called when a connection's timeout was reached */
IODINE_CONNECTION_DEF_CB(on_timeout, IODINE_ON_TIMEOUT_ID);
/** Called after the connection was closed (called once per IO). */
static void iodine_io_raw_on_close(void *udata) {
  iodine_ruby_call_outside((VALUE)udata, IODINE_CALL_ID, 0, NULL);
  STORE.release((VALUE)udata);
}

#undef IODINE_CONNECTION_DEF_CB

static fio_protocol_s IODINE_RAW_PROTOCOL = {
    .on_attach = iodine_io_raw_on_attach,
    .on_data = iodine_io_raw_on_data,
    .on_ready = iodine_io_raw_on_ready,
    .on_timeout = iodine_io_raw_on_timeout,
    .on_shutdown = iodine_io_raw_on_shutdown,
    .on_close = iodine_io_raw_on_close,
};

/* *****************************************************************************
HTTP / WebSockets Callbacks and Helpers
***************************************************************************** */
#define IODINE_CONNECTION_DEF_CB(named, id)                                    \
  static void iodine_io_http_##named(fio_http_s *h) {                          \
    VALUE handler = (VALUE)fio_http_udata(h);                                  \
    if (!handler || handler == Qnil)                                           \
      return;                                                                  \
    iodine_ruby_call_outside(handler, id, 0, NULL);                            \
  }

/** TODO: fix me */
IODINE_CONNECTION_DEF_CB(on_http, IODINE_ON_OPEN_ID);

/** Called when an IO is attached to the protocol. */
IODINE_CONNECTION_DEF_CB(on_open, IODINE_ON_OPEN_ID);
/** called once all pending `write` calls are finished. */
IODINE_CONNECTION_DEF_CB(on_ready, IODINE_ON_READY_ID);
/** called if the connection is open when the server is shutting down. */
IODINE_CONNECTION_DEF_CB(on_shutdown, IODINE_ON_SHUTDOWN_ID);
/** Called when a connection's timeout was reached */
IODINE_CONNECTION_DEF_CB(on_timeout, IODINE_ON_TIMEOUT_ID);
/** Called after the connection was closed (called once per IO). */
IODINE_CONNECTION_DEF_CB(on_close, IODINE_ON_TIMEOUT_ID);

// pre_http_body
// on_open
// on_ready
// on_shutdown
// on_close

// on_authenticate_sse // returns an int
// on_authenticate_websocket // returns an int
// /** Called when a WebSocket message is received. */
// static void iodine_http_on_message(fio_http_s *h,
//                                    fio_buf_info_s msg,
//                                    uint8_t is_text);

// /** Called when an EventSource event is received. */
// static void iodine_http_on_eventsource(fio_http_s *h,
//                                        fio_buf_info_s id,
//                                        fio_buf_info_s event,
//                                        fio_buf_info_s data);

// /** Called when an EventSource reconnect event requests an ID. */
// static void iodine_http_on_eventsource_reconnect(fio_http_s *h,
// fio_buf_info_s id);

#undef IODINE_CONNECTION_DEF_CB

/** Called when a websocket / SSE message arrives. */
static void iodine_io_http_on_message(fio_http_s *h,
                                      fio_buf_info_s msg,
                                      uint8_t is_text) {
  VALUE handler = (VALUE)fio_http_udata(h);
  if (!handler || handler == Qnil)
    return;
  iodine_ruby_call_outside(handler, IODINE_ON_HTTP_ID, 0, NULL);
}

static fio_http_settings_s IODINE_HTTP_SETTINGS = {
    .on_http = iodine_io_http_on_http,
    .on_message = iodine_io_http_on_message,
    .on_ready = iodine_io_http_on_ready,
    .on_shutdown = iodine_io_http_on_shutdown,
    .on_close = iodine_io_http_on_close,
};

/* *****************************************************************************
HTTP IO Callbacks and Helpers
***************************************************************************** */

/** Called before body uploads, when a client sends an `Expect` header. */
static void iodine_io_http_pre_http_body(fio_http_s *h);
/** Callback for HTTP requests (server) or responses (client). */
static void iodine_io_http_on_http(fio_http_s *h);
/** (optional) the callback to be performed when the HTTP service closes. */
static void iodine_io_http_on_finish(struct fio_http_settings_s *settings);

/** Authenticate EventSource (SSE) requests, return non-zero to deny.*/
static int iodine_io_http_on_authenticate_sse(fio_http_s *h);
/** Authenticate WebSockets Upgrade requests, return non-zero to deny.*/
static int iodine_io_http_on_authenticate_websocket(fio_http_s *h);

/** Called once a WebSocket / SSE connection upgrade is complete. */
static void iodine_io_http_on_open(fio_http_s *h);

/** Called when a WebSocket message is received. */
static void iodine_io_http_on_message(fio_http_s *h,
                                      fio_buf_info_s msg,
                                      uint8_t is_text);
/** Called when an EventSource event is received. */
static void iodine_io_http_on_eventsource(fio_http_s *h,
                                          fio_buf_info_s id,
                                          fio_buf_info_s event,
                                          fio_buf_info_s data);
/** Called when an EventSource reconnect event requests an ID. */
static void iodine_io_http_on_eventsource_reconnect(fio_http_s *h,
                                                    fio_buf_info_s id);

/** Called for WebSocket / SSE connections when outgoing buffer is empty. */
static void iodine_io_http_on_ready(fio_http_s *h);
/** Called for open WebSocket / SSE connections during shutting down. */
static void iodine_io_http_on_shutdown(fio_http_s *h);
/** Called after a WebSocket / SSE connection is closed (for cleanup). */
static void iodine_io_http_on_close(fio_http_s *h);

/* *****************************************************************************
Subscription Helpers
***************************************************************************** */

/* a pub/sub message struct */
static VALUE Iodine_Message;

static void *iodine_connection_on_pubsub_in_gvl(void *m_) {
  fio_msg_s *m = (fio_msg_s *)m_;
  VALUE msg = iodine_pubsub_msg_create(m);
  iodine_ruby_call_inside((VALUE)m->udata, IODINE_CALL_ID, 1, &msg);
  STORE.release(msg);
}

static void iodine_connection_on_pubsub(fio_msg_s *m) { /* TODO! */
  rb_thread_call_without_gvl(iodine_connection_on_pubsub_in_gvl, m, NULL, NULL);
}

FIO_IFUNC VALUE iodine_connection_subscribe_internal(fio_s *io,
                                                     int argc,
                                                     VALUE *argv) {
  VALUE name = Qnil;
  VALUE filter = Qnil;
  VALUE proc = Qnil;
  long long filter_i = 0;
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  fio_rb_multi_arg(argc,
                   argv,
                   FIO_RB_ARG(name, 0, "channel", Qnil, 0),
                   FIO_RB_ARG(filter, 0, "filter", Qnil, 0),
                   FIO_RB_ARG(proc, 0, "proc", Qnil, 0));
  if (RB_TYPE_P(name, RUBY_T_SYMBOL))
    name = rb_sym_to_s(name);
  if (name != Qnil) {
    rb_check_type(name, RUBY_T_STRING);
    channel = FIO_BUF_INFO2(RSTRING_PTR(name), (size_t)RSTRING_LEN(name));
  }
  if (filter != Qnil) {
    rb_check_type(filter, RUBY_T_FIXNUM);
    filter_i = RB_NUM2LL(filter);
    if ((size_t)filter_i & (~(size_t)0xFFFF))
      rb_raise(rb_eRangeError, "filter out of range (%lld > 0xFFFF)", filter_i);
  }
  if (rb_block_given_p() && proc == Qnil)
    proc = rb_block_proc();
  else if (proc != Qnil && !rb_respond_to(proc, rb_intern2("call", 4)))
    rb_raise(rb_eArgError, "a callback object MUST respond to `call`");
  if (!io && proc == Qnil)
    rb_raise(rb_eArgError,
             "Global subscriptions require a callback (proc/block) object!");
  STORE.hold(proc);
  fio_subscribe(.io = io,
                .filter = (int16_t)filter_i,
                .channel = channel,
                .udata = (void *)proc,
                .on_message =
                    ((proc == Qnil) ? NULL : iodine_connection_on_pubsub),
                .on_unsubscribe = (void (*)(void *))STORE.release);
}

static VALUE iodine_connection_unsubscribe_internal(fio_s *io,
                                                    int argc,
                                                    VALUE *argv) {
  VALUE name = Qnil;
  VALUE filter = Qnil;
  long long filter_i = 0;
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  fio_rb_multi_arg(argc,
                   argv,
                   FIO_RB_ARG(name, 0, "channel", Qnil, 0),
                   FIO_RB_ARG(filter, 0, "filter", Qnil, 0));
  if (RB_TYPE_P(name, RUBY_T_SYMBOL))
    name = rb_sym_to_s(name);
  if (name != Qnil) {
    rb_check_type(name, RUBY_T_STRING);
    channel = FIO_BUF_INFO2(RSTRING_PTR(name), (size_t)RSTRING_LEN(name));
  }
  if (filter != Qnil) {
    rb_check_type(filter, RUBY_T_FIXNUM);
    filter_i = RB_NUM2LL(filter);
    if ((size_t)filter_i & (~(size_t)0xFFFF))
      rb_raise(rb_eRangeError, "filter out of range (%lld > 0xFFFF)", filter_i);
  }
  fio_unsubscribe(.io = io, .channel = channel, .filter = filter_i);
}

/* *****************************************************************************
Ruby Public API.
***************************************************************************** */

/** Initializes a Connection object. */
static VALUE iodine_connection_initialize(int argc, VALUE *argv, VALUE self) {
  rb_raise(rb_eException, "Iodine::Connection.new shouldn't be called!");
  return self;
}

/** Returns true if the connection appears to be open (no known issues). */
static VALUE iodine_connection_is_open(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (c && c->io)
    return fio_srv_is_open(c->io);
  return Qfalse;
}

/**
 * Returns the number of bytes that need to be sent before the next `on_drained`
 * callback is called.
 */
static VALUE iodine_connection_pending(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (c && c->io)
    return RB_SIZE2NUM(((size_t)fio_stream_length(&c->io->stream)));
  return Qfalse;
}

/** Schedules the connection to be closed. */
static VALUE iodine_connection_close(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (c) {
    if (c->http)
      fio_http_close(c->http);
    else if (c->io)
      fio_close(c->io);
  }
  return self;
}

/**
 * Always returns true, since Iodine connections support the pub/sub extension.
 */
static VALUE iodine_connection_has_pubsub(VALUE self) { return Qtrue; }

/** Writes data to the connection asynchronously. */
static VALUE iodine_connection_write(VALUE self, VALUE data) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c || (!c->http && !c->io))
    return Qnil;
  fio_str_info_s to_write;
  unsigned to_copy = 1;
  void (*dealloc)(void *) = NULL;
  if (RB_TYPE_P(data, RUBY_T_SYMBOL))
    data = rb_sym_to_s(data);
  if (RB_TYPE_P(data, RUBY_T_STRING)) {
    to_write = FIO_STR_INFO2(RSTRING_PTR(data), (size_t)RSTRING_LEN(data));
  } else {
    dealloc = (void (*)(void *))fio_bstr_free;
    to_copy = 0;
    to_write = fio_bstr_info(iodine_json_stringify2bstr(NULL, data));
  }
  if (c->http)
    fio_http_write(c->http,
                   .buf = to_write.buf,
                   .len = to_write.len,
                   .dealloc = dealloc,
                   .copy = to_copy);
  else if (c->io)
    fio_write2(c->io,
               .buf = to_write.buf,
               .len = to_write.len,
               .dealloc = dealloc,
               .copy = to_copy);
  return Qtrue;
}

/**
 * Un-Subscribes to a combination of a channel (String) and filter (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the subscription's channel name (String).
 * - `filter`:  the subscription's filter (Number < 32,767).
 *
 * i.e.:
 *
 *     unsubscribe("name")
 *     unsubscribe(channel: "name")
 *     # or with filter
 *     unsubscribe("name", 1)
 *     unsubscribe(channel: "name", filter: 1)
 *     # or only a filter
 *     unsubscribe(nil, 1)
 *     unsubscribe(filter: 1)
 *
 */
static VALUE iodine_connection_unsubscribe(int argc, VALUE *argv, VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c || (!c->http && !c->io))
    return Qnil;
  iodine_connection_unsubscribe_internal(c->io, argc, argv);
  return self;
}

/**
 * Un-Subscribes to a combination of a channel (String) and filter (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the subscription's channel name (String).
 * - `filter`:  the subscription's filter (Number < 32,767).
 *
 * i.e.:
 *
 *     unsubscribe("name")
 *     unsubscribe(channel: "name")
 *     # or with filter
 *     unsubscribe("name", 1)
 *     unsubscribe(channel: "name", filter: 1)
 *     # or only a filter
 *     unsubscribe(nil, 1)
 *     unsubscribe(filter: 1)
 *
 */
static VALUE iodine_connection_unsubscribe_klass(int argc,
                                                 VALUE *argv,
                                                 VALUE self) {
  iodine_connection_unsubscribe_internal(NULL, argc, argv);
  return self;
}

/**
 * Subscribes to a combination of a channel (String) and filter (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the subscription's channel name (String).
 * - `filter`:  the subscription's filter (Number < 32,767).
 * - `proc`:    an optional object that answers to `call` and accepts a single
 *              argument (the message structure).
 *
 * The message structure (the argument passed to `proc`) answers to the
 * following:
 *
 * - `id`: the message's (somewhat unique) ID (Number).
 * - `channel`: the message's target channel name (String or `nil`).
 * - `filter`:  the message's target filter (Number < 32,767).
 * - `message`:  the message's content (String or `nil`).
 * - `published`: a Unix timestamp in Milliseconds (Number).
 *
 * i.e.:
 *
 *     subscribe("name")
 *     subscribe(channel: "name")
 *     # or with filter
 *     subscribe("name", 1)
 *     subscribe(channel: "name", filter: 1)
 *     # or only a filter
 *     subscribe(nil, 1)
 *     subscribe(filter: 1)
 *     # with / without a proc
 *     subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil; }
 *     subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
 *
 */
static VALUE iodine_connection_subscribe(int argc, VALUE *argv, VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c || (!c->http && !c->io))
    return Qnil;
  if (c->io)
    iodine_connection_subscribe_internal(c->io, argc, argv);
  else
    iodine_connection_subscribe_internal(fio_http_io(c->http), argc, argv);
  return self;
}

/**
 * Subscribes to a combination of a channel (String) and filter (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the subscription's channel name (String).
 * - `filter`:  the subscription's filter (Number < 32,767).
 * - `proc`:    an optional object that answers to `call` and accepts a single
 *              argument (the message structure).
 *
 * Either a `proc` or a `block` MUST be provided for global subscriptions.
 *
 * i.e.:
 *
 *     Iodine.subscribe("name")
 *     Iodine.subscribe(channel: "name")
 *     # or with filter
 *     Iodine.subscribe("name", 1)
 *     Iodine.subscribe(channel: "name", filter: 1)
 *     # or only a filter
 *     Iodine.subscribe(nil, 1)
 *     Iodine.subscribe(filter: 1)
 *     # with / without a proc
 *     Iodine.subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil;}
 *     Iodine.subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
 *
 */
static VALUE iodine_connection_subscribe_klass(int argc,
                                               VALUE *argv,
                                               VALUE self) {
  iodine_connection_subscribe_internal(NULL, argc, argv);
  return self;
}

/**
 * Publishes a message to a combination of a channel (String) and filter
 * (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the channel name to publish to (String).
 * - `filter`:  the filter to publish to (Number < 32,767).
 * - `message`: the actual message to publish.
 * - `engine`:  the pub/sub engine to use (if not the default one).
 *
 * i.e.:
 *
 *     Iodine.publish("channel_name", 0, "payload")
 *     Iodine.publish(channel: "name", msg: "payload")
 */
static VALUE iodine_connection_publish(int argc, VALUE *argv, VALUE self) {
  VALUE name = Qnil;
  VALUE filter = Qnil;
  VALUE message = Qnil;
  VALUE engine = Qnil;
  long long filter_i = 0;
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  fio_buf_info_s msg = FIO_BUF_INFO2(NULL, 0);
  fio_rb_multi_arg(argc,
                   argv,
                   FIO_RB_ARG(name, 0, "channel", Qnil, 0),
                   FIO_RB_ARG(filter, 0, "filter", Qnil, 0),
                   FIO_RB_ARG(message, 0, "message", Qnil, 0),
                   FIO_RB_ARG(engine, 0, "engine", Qnil, 0));

  if (RB_TYPE_P(name, RUBY_T_SYMBOL))
    name = rb_sym_to_s(name);
  if (name != Qnil) {
    rb_check_type(name, RUBY_T_STRING);
    channel = FIO_BUF_INFO2(RSTRING_PTR(name), (size_t)RSTRING_LEN(name));
  }
  if (filter != Qnil) {
    rb_check_type(filter, RUBY_T_FIXNUM);
    filter_i = RB_NUM2LL(filter);
    if ((size_t)filter_i & (~(size_t)0xFFFF))
      rb_raise(rb_eRangeError, "filter out of range (%lld > 0xFFFF)", filter_i);
  }
  rb_check_type(message, RUBY_T_STRING);
  msg = FIO_BUF_INFO2(RSTRING_PTR(message), (size_t)RSTRING_LEN(message));
  fio_publish(.filter = (int16_t)filter_i, .channel = channel, .message = msg);
}

/* *****************************************************************************
Listen to incoming TCP/IP Connections
***************************************************************************** */

// intptr_t iodine_tcp_listen(iodine_connection_args_s args) {
//   IodineStore.add(args.handler);
//   return fio_srv_listen(.port = args.port.data,
//                         .address = args.address.data,
//                         .on_open = iodine_tcp_on_open,
//                         .on_finish = iodine_tcp_on_finish,
//                         .tls = args.tls,
//                         .udata = (void *)args.handler);
// }

/* *****************************************************************************
Listen to incoming HTTP Connections
***************************************************************************** */

static void iodine_io_http_on_finish(struct fio_http_settings_s *s) {
  STORE.release((VALUE)s->udata);
}

// clang-format off
/**
Listens to incoming HTTP connections and handles incoming requests using the
Rack specification.

This is delegated to a lower level C HTTP and Websocket implementation, no
Ruby object will be crated except the `env` object required by the Rack
specifications.

Accepts a single Hash argument with the following properties:

(it's possible to set default values using the {Iodine::DEFAULT_HTTP_ARGS} Hash)

app:: the Rack application that handles incoming requests. Default: `nil`.
port:: the port to listen to. Default: 3000.
address:: the address to bind to. Default: binds to all possible addresses.
log:: enable response logging (Hijacked sockets aren't logged). Default: off.
public:: The root public folder for static file service. Default: none.
timeout:: Timeout for inactive HTTP/1.x connections. Defaults: 40 seconds.
max_body:: The maximum body size for incoming HTTP messages in bytes. Default: ~50Mib.
max_headers:: The maximum total header length for incoming HTTP messages. Default: ~64Kib.
max_msg:: The maximum Websocket message size allowed. Default: ~250Kib.
ping:: The Websocket `ping` interval. Default: 40 seconds.

Either the `app` or the `public` properties are required. If niether exists,
the function will fail. If both exist, Iodine will serve static files as well
as dynamic requests.

When using the static file server, it's possible to serve `gzip` versions of
the static files by saving a compressed version with the `gz` extension (i.e.
`styles.css.gz`).

`gzip` will only be served to clients tat support the `gzip` transfer
encoding.

Once HTTP/2 is supported (planned, but probably very far away), HTTP/2
timeouts will be dynamically managed by Iodine. The `timeout` option is only
relevant to HTTP/1.x connections.
*/
// intptr_t iodine_http_listen(iodine_connection_args_s args){
//   // clang-format on
//   if (args.public.data) {
//     rb_hash_aset(env_template_no_upgrade, XSENDFILE_TYPE, XSENDFILE);
//     rb_hash_aset(env_template_no_upgrade, XSENDFILE_TYPE_HEADER, XSENDFILE);
//     support_xsendfile = 1;
//   }
//   IodineStore.add(args.handler);
// #ifdef __MINGW32__
//   intptr_t uuid = http_listen(args.port.data,
//                               args.address.data,
//                               .on_request = on_rack_request,
//                               .on_upgrade = on_rack_upgrade,
//                               .udata = (void *)args.handler,
//                               .timeout = args.timeout,
//                               .ws_timeout = args.ping,
//                               .ws_max_msg_size = args.max_msg,
//                               .max_header_size = args.max_headers,
//                               .on_finish = free_iodine_http,
//                               .log = args.log,
//                               .max_body_size = args.max_body,
//                               .public_folder = args.public.data);
// #else
//   intptr_t uuid = http_listen(args.port.data,
//                               args.address.data,
//                               .on_request = on_rack_request,
//                               .on_upgrade = on_rack_upgrade,
//                               .udata = (void *)args.handler,
//                               .tls = args.tls,
//                               .timeout = args.timeout,
//                               .ws_timeout = args.ping,
//                               .ws_max_msg_size = args.max_msg,
//                               .max_header_size = args.max_headers,
//                               .on_finish = free_iodine_http,
//                               .log = args.log,
//                               .max_body_size = args.max_body,
//                               .public_folder = args.public.data);
// #endif
//   if (uuid == -1)
//     return uuid;

//   if ((args.handler == Qnil || args.handler == Qfalse)) {
//     FIO_LOG_WARNING("(listen) no handler / app, the HTTP service on port %s "
//                     "will only serve "
//                     "static files.",
//                     args.port.data ? args.port.data : args.address.data);
//   }
//   if (args.public.data) {
//     FIO_LOG_INFO("Serving static files from %s", args.public.data);
//   }

//   return uuid;
// }

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
// static VALUE iodine_listen(VALUE self, VALUE args) {
//   // clang-format on
//   iodine_connection_args_s s = iodine_connect_args(args, 1);
//   intptr_t uuid = -1;
//   switch (s.service) {
//   case IODINE_SERVICE_RAW: uuid = iodine_tcp_listen(s); break;
//   case IODINE_SERVICE_HTTP: /* overflow */
//   case IODINE_SERVICE_WS: uuid = iodine_http_listen(s); break;
//   }
//   iodine_connect_args_cleanup(&s);
//   if (uuid == -1)
//     rb_raise(rb_eRuntimeError, "Couldn't open listening socket.");
//   return s.handler;
//   (void)self;
// }

/* *****************************************************************************
Initialize Connection Class
***************************************************************************** */

// clang-format off
/**
module MyConnectionCallbacks

  # called when the callback object is linked with a new client
  def on_open client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when data is available
  def on_message client, data
     client.is_a?(Iodine::Connection) # => true
  end

  # called when the server is shutting down, before closing the client
  # (it's still possible to send messages to the client)
  def on_shutdown client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when the client is closed (no longer available)
  def on_close client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when all the previous calls to `client.write` have completed
  # (the local buffer was drained and is now empty)
  def on_drained client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when timeout was reached, allowing a `ping` to be sent
  def ping client
     client.is_a?(Iodine::Connection) # => true
     clint.close() # close connection on timeout is the default
  end

  # Allows the module to be used as a static callback object (avoiding object allocation)
  extend self
end
All connection related actions can be performed using the methods provided through this class.


#close ⇒ Object
Schedules the connection to be closed.

#publish(*args) ⇒ Object
Publishes a message to a channel.

#subscribe(*args) ⇒ Object
Subscribes to a Pub/Sub stream / channel or replaces an existing subscription.

#unsubscribe(name) ⇒ Object
Unsubscribes from a Pub/Sub stream / channel.

#write(data) ⇒ Object
Writes data to the connection asynchronously.

#protocol ⇒ Object
Returns the connection's protocol Symbol (:sse, :websocket or :raw).

*/
static void Init_iodine_connection(void) { // clang-format on
  VALUE m = rb_define_class_under(iodine_rb_IODINE, "Connection", rb_cObject);
  rb_define_alloc_func(m, iodine_connection_alloc);

  rb_define_method(m, "initialize", iodine_connection_initialize, -1);

  rb_define_method(m, "env", iodine_connection_env_get, 0);
  rb_define_method(m, "env=", iodine_connection_env_set, 1);
  rb_define_method(m, "handler", iodine_connection_handler_get, 0);
  rb_define_method(m, "handler=", iodine_connection_handler_set, 1);
  rb_define_method(m, "open?", iodine_connection_is_open, 0);
  rb_define_method(m, "pubsub?", iodine_connection_has_pubsub, 0);
  rb_define_method(m, "pending", iodine_connection_pending, 0);
  rb_define_method(m, "close", iodine_connection_close, 0);
  rb_define_method(m, "write", iodine_connection_write, 1);

  rb_define_method(m, "subscribe", iodine_connection_subscribe, -1);
  rb_define_singleton_method(m,
                             "subscribe",
                             iodine_connection_subscribe_klass,
                             -1);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "subscribe",
                             iodine_connection_subscribe_klass,
                             -1);

  rb_define_method(m, "unsubscribe", iodine_connection_unsubscribe, -1);
  rb_define_singleton_method(m,
                             "unsubscribe",
                             iodine_connection_unsubscribe_klass,
                             -1);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "unsubscribe",
                             iodine_connection_unsubscribe_klass,
                             -1);

  rb_define_method(m, "publish", iodine_connection_publish, -1);
  rb_define_singleton_method(m, "publish", iodine_connection_publish, -1);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "publish",
                             iodine_connection_publish,
                             -1);

  iodine_pubsub_msg_init();
  iodine_connection_init_env_template(FIO_BUF_INFO0);
  iodine_tls_init();
}
