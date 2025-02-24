#ifndef H___IODINE_CONNECTION___H
/** ****************************************************************************
Iodine::Connection

This module actually covers a LOT of the functionality of the Iodine server.

Everything that has to do with establishing and managing connections goes here.

***************************************************************************** */
#define H___IODINE_CONNECTION___H
#include "iodine.h"
/* *****************************************************************************
Constants Used only by Iodine::Connection
***************************************************************************** */
static ID IODINE_SAME_SITE_DEFAULT;
static ID IODINE_SAME_SITE_NONE;
static ID IODINE_SAME_SITE_LAX;
static ID IODINE_SAME_SITE_STRICT;

/* *****************************************************************************
Ruby Connection Object
***************************************************************************** */

typedef enum {
  IODINE_CONNECTION_STORE_handler = 0,
  IODINE_CONNECTION_STORE_env,
  IODINE_CONNECTION_STORE_rack,
  IODINE_CONNECTION_STORE_method,
  IODINE_CONNECTION_STORE_path,
  IODINE_CONNECTION_STORE_query,
  IODINE_CONNECTION_STORE_version,
  IODINE_CONNECTION_STORE_tmp,
  IODINE_CONNECTION_STORE_FINISH,
} iodine_connection_store_e;

typedef enum {
  IODINE_CONNECTION_HEADERS_COPIED = 1,
  IODINE_CONNECTION_UPGRADE_SSE = 2,
  IODINE_CONNECTION_UPGRADE_WS = 4,
  IODINE_CONNECTION_UPGRADE = (2 | 4),
  IODINE_CONNECTION_CLOSED = 8,
  IODINE_CONNECTION_CLIENT = 16,
} iodine_connection_flags_e;

typedef struct iodine_connection_s {
  fio_io_s *io;
  fio_http_s *http;
  VALUE store[IODINE_CONNECTION_STORE_FINISH];
  iodine_minimap_s map;
  iodine_connection_flags_e flags;
} iodine_connection_s;

static size_t iodine_connection_data_size(const void *ptr_) {
  iodine_connection_s *c = (iodine_connection_s *)ptr_;
  return sizeof(*c) +
         (iodine_hmap_count(&c->map.map) * sizeof(c->map.map.map[0]));
}

static void iodine_connection_free(void *ptr_) {
  iodine_connection_s *c = (iodine_connection_s *)ptr_;
  fio_http_free(c->http);
  iodine_hmap_destroy(&c->map.map);
  FIO_LEAK_COUNTER_ON_FREE(iodine_connection);
  ruby_xfree(c);
  // fio_free(c);
}

static void iodine_connection_mark(void *ptr_) {
  iodine_connection_s *c = (iodine_connection_s *)ptr_;
  for (size_t i = 0; i < IODINE_CONNECTION_STORE_FINISH; ++i)
    if (!IODINE_STORE_IS_SKIP(c->store[i]))
      rb_gc_mark(c->store[i]);
  iodine_minimap_gc_mark(&c->map);
}

static const rb_data_type_t IODINE_CONNECTION_DATA_TYPE = {
    .wrap_struct_name = "IodineConnection",
    .function =
        {
            .dmark = iodine_connection_mark,
            .dfree = iodine_connection_free,
            .dsize = iodine_connection_data_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE iodine_connection_alloc(VALUE klass) {
  // iodine_connection_s *c = (iodine_connection_s *)fio_malloc(sizeof(*c));
  // FIO_ASSERT_ALLOC(c);
  // VALUE self = TypedData_Wrap_Struct(klass, &IODINE_CONNECTION_DATA_TYPE, c);

  iodine_connection_s *c = NULL;
  VALUE self = TypedData_Make_Struct(klass,
                                     iodine_connection_s,
                                     &IODINE_CONNECTION_DATA_TYPE,
                                     c);
  *c = (iodine_connection_s){0};
  for (size_t i = 0; i < IODINE_CONNECTION_STORE_FINISH; ++i)
    c->store[i] = Qnil;
  FIO_LEAK_COUNTER_ON_ALLOC(iodine_connection);
  return self;
}

static iodine_connection_s *iodine_connection_ptr(VALUE self) {
  iodine_connection_s *c;
  TypedData_Get_Struct(self,
                       iodine_connection_s,
                       &IODINE_CONNECTION_DATA_TYPE,
                       c);
  return c;
}

/** Creates (and allocates) a new Iodine::Connection object. */
static VALUE iodine_connection_create_from_io(fio_io_s *io) {
  VALUE m = rb_obj_alloc(iodine_rb_IODINE_CONNECTION);
  STORE.hold(m);
  iodine_connection_s *c = iodine_connection_ptr(m);
  c->store[IODINE_CONNECTION_STORE_handler] = (VALUE)fio_io_udata(io);
  c->io = io;
  c->http = NULL;
  fio_io_udata_set(io, (void *)m);
  return m;
}

/** Creates (and allocates) a new Iodine::Connection object. */
static VALUE iodine_connection_create_from_http(fio_http_s *h) {
  VALUE m = fio_http_udata2(h)
                ? (VALUE)fio_http_udata2(h)
                : iodine_connection_alloc(iodine_rb_IODINE_CONNECTION);
  STORE.hold(m);
  iodine_connection_s *c = iodine_connection_ptr(m);
  c->store[IODINE_CONNECTION_STORE_handler] = (VALUE)fio_http_udata(h);
  c->io = fio_http_io(h);
  if (!(c->flags & IODINE_CONNECTION_CLIENT))
    c->http = fio_http_dup(h);
  fio_http_udata2_set(h, (void *)m);
  return m;
}

/* *****************************************************************************
State and Misc
***************************************************************************** */

static VALUE iodine_connection_headers_sent_p(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http)
    return Qtrue;
  return (fio_http_is_clean(c->http) ? Qfalse : Qtrue);
}

static VALUE iodine_connection_is_clean(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (c->http)
    return (fio_http_is_clean(c->http) ? Qfalse : Qtrue);
  if (c->io)
    return (fio_io_is_open(c->io) ? Qtrue : Qfalse);
  return Qfalse;
}

static VALUE iodine_connection_dup_failer(VALUE o) {
  rb_raise(rb_eTypeError, "Iodine::Connection objects can't be duplicated.");
  return o;
}

static VALUE iodine_connection_to_s(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (c->http)
    return STORE.frozen_str(FIO_STR_INFO1((char *)"Iodine::Connection(HTTP)"));
  if (c->io)
    return STORE.frozen_str(FIO_STR_INFO1((char *)"Iodine::Connection(RAW)"));
  return STORE.frozen_str(FIO_STR_INFO1((char *)"Iodine::Connection(NONE)"));
}

static VALUE iodine_connection_is_websocket(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (c->http && fio_http_is_websocket(c->http))
    return Qtrue;
  return Qfalse;
}

static VALUE iodine_connection_is_sse(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (c->http && fio_http_is_sse(c->http))
    return Qtrue;
  return Qfalse;
}

/* *****************************************************************************
Ruby Store Get/Set
***************************************************************************** */

#define IODINE_DEF_GET_FUNC(val_name)                                          \
  /** Returns the client's current val_name object. */                         \
  static VALUE iodine_connection_##val_name##_get(VALUE self) {                \
    iodine_connection_s *c = iodine_connection_ptr(self);                      \
    return c->store[IODINE_CONNECTION_STORE_##val_name];                       \
  }
#define IODINE_DEF_SET_FUNC(val_name)                                          \
  /** Sets the client's val_name object. */                                    \
  static VALUE iodine_connection_##val_name##_set(VALUE self,                  \
                                                  VALUE updated_val) {         \
    iodine_connection_s *c = iodine_connection_ptr(self);                      \
    c->store[IODINE_CONNECTION_STORE_##val_name] = updated_val;                \
    return updated_val;                                                        \
  }

IODINE_DEF_GET_FUNC(handler);
IODINE_DEF_SET_FUNC(env);

#undef IODINE_DEF_GET_FUNC
#undef IODINE_DEF_SET_FUNC

/* *****************************************************************************
Ruby Store Get/Set HTTP Properties
***************************************************************************** */

#define IODINE_DEF_GETSET_FUNC(val_name, frozen_)                              \
  /** Returns the client's current val_name object. */                         \
  static VALUE iodine_connection_##val_name##_get(VALUE self) {                \
    iodine_connection_s *c = iodine_connection_ptr(self);                      \
    fio_str_info_s setter;                                                     \
    if (!c)                                                                    \
      return Qnil;                                                             \
    if (FIO_UNLIKELY(c->http &&                                                \
                     c->store[IODINE_CONNECTION_STORE_##val_name] == Qnil) &&  \
        (setter = fio_http_##val_name(c->http)).len) {                         \
      c->store[IODINE_CONNECTION_STORE_##val_name] =                           \
          (frozen_ ? STORE.frozen_str(setter)                                  \
                   : rb_str_new(setter.buf, setter.len));                      \
    }                                                                          \
    return c->store[IODINE_CONNECTION_STORE_##val_name];                       \
  }                                                                            \
  /** Sets the client's val_name object. */                                    \
  static VALUE iodine_connection_##val_name##_set(VALUE self,                  \
                                                  VALUE updated_val) {         \
    iodine_connection_s *c = iodine_connection_ptr(self);                      \
    if (!c)                                                                    \
      return Qnil;                                                             \
    c->store[IODINE_CONNECTION_STORE_##val_name] = updated_val;                \
    if (!c->http)                                                              \
      return updated_val;                                                      \
    if (RB_TYPE_P(updated_val, RUBY_T_SYMBOL))                                 \
      updated_val = rb_sym2str(updated_val);                                   \
    if (!RB_TYPE_P(updated_val, RUBY_T_STRING))                                \
      rb_raise(rb_eTypeError, "new value must be a String or Symbol.");        \
    fio_http_##val_name##_set(                                                 \
        c->http,                                                               \
        FIO_STR_INFO2(RSTRING_PTR(updated_val),                                \
                      (size_t)RSTRING_LEN(updated_val)));                      \
    return updated_val;                                                        \
  }

/* String Get/Set: */
IODINE_DEF_GETSET_FUNC(method, 1)
IODINE_DEF_GETSET_FUNC(path, 0)
IODINE_DEF_GETSET_FUNC(query, 0)
IODINE_DEF_GETSET_FUNC(version, 1)
#undef IODINE_DEF_GETSET_FUNC

#define IODINE_DEF_GETSET_FUNC(val_name)                                       \
  /** Returns the client's current val_name object. */                         \
  static VALUE iodine_connection_##val_name##_get(VALUE self) {                \
    iodine_connection_s *c = iodine_connection_ptr(self);                      \
    if (FIO_UNLIKELY(!c || !c->http))                                          \
      return Qnil;                                                             \
    const size_t setter = fio_http_##val_name(c->http);                        \
    return ULL2NUM(setter);                                                    \
  }                                                                            \
  /** Sets the client's val_name object. */                                    \
  static VALUE iodine_connection_##val_name##_set(VALUE self,                  \
                                                  VALUE updated_val) {         \
    iodine_connection_s *c = iodine_connection_ptr(self);                      \
    size_t setter = 0;                                                         \
    if (FIO_UNLIKELY(!c || !c->http))                                          \
      return Qnil;                                                             \
    if (RB_TYPE_P(updated_val, RUBY_T_FIXNUM))                                 \
      setter = NUM2ULL(updated_val);                                           \
    else if (!RB_TYPE_P(updated_val, RUBY_T_STRING)) {                         \
      fio_buf_info_s s = IODINE_RSTR_INFO(updated_val);                        \
      const char *end = s.buf + s.len;                                         \
      setter = fio_atol(&s.buf);                                               \
      if (s.buf != end)                                                        \
        rb_raise(rb_eArgError,                                                 \
                 "passed String isn't a number: %.*s",                         \
                 (int)s.len,                                                   \
                 end - s.len);                                                 \
    } else                                                                     \
      rb_raise(                                                                \
          rb_eTypeError,                                                       \
          "new value must be a Number (or a String containing a number).");    \
    fio_http_##val_name##_set(c->http, setter);                                \
    return ULL2NUM(setter);                                                    \
  }

/* Numeral Get/Set: */
IODINE_DEF_GETSET_FUNC(status)
#undef IODINE_DEF_GETSET_FUNC

/* *****************************************************************************
Hash Map style access
***************************************************************************** */

FIO_SFUNC int iodine_connection_map_headers_task(fio_http_s *h,
                                                 fio_str_info_s name,
                                                 fio_str_info_s value,
                                                 void *c_) {
  iodine_connection_s *c = (iodine_connection_s *)c_;
  VALUE ary = Qnil;
  VALUE k = STORE.frozen_str(name);
  c->store[IODINE_CONNECTION_STORE_tmp] = k;
  VALUE tmp = iodine_minimap_rb_get(&c->map, k);
  if (FIO_LIKELY(!tmp)) {
    tmp = rb_str_new(value.buf, value.len);
    iodine_hmap_set(&c->map.map, k, tmp, NULL);
    c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
    return 0;
  }
  if (RB_TYPE_P(tmp, RUBY_T_STRING)) {
    ary = rb_ary_new();
    c->store[IODINE_CONNECTION_STORE_rack] = ary; /* unused, so use it */
    rb_ary_push(ary, tmp);
    iodine_hmap_set(&c->map.map, k, ary, NULL);
    c->store[IODINE_CONNECTION_STORE_rack] = Qnil;
    tmp = ary;
  }
  rb_ary_push(tmp, rb_str_new(value.buf, value.len));
  c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  return 0;
}

FIO_SFUNC VALUE iodine_connection_map_headers(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http || (fio_atomic_or(&c->flags, IODINE_CONNECTION_HEADERS_COPIED) &
                   IODINE_CONNECTION_HEADERS_COPIED))
    return o;
  if ((c->flags & IODINE_CONNECTION_CLIENT))
    goto is_client;
  iodine_hmap_reserve(&c->map.map,
                      fio_http_request_header_count(c->http, FIO_STR_INFO0));
  fio_http_request_header_each(c->http, iodine_connection_map_headers_task, c);
  return o;

is_client:
  iodine_hmap_reserve(&c->map.map,
                      fio_http_response_header_count(c->http, FIO_STR_INFO0));
  fio_http_response_header_each(c->http, iodine_connection_map_headers_task, c);
  fio_http_cookie(c->http, NULL, 0); /* force parsing of cookies */
  return o;
}

static iodine_minimap_s *iodine_connection_map_ptr(VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  return &c->map;
}

static VALUE iodine_connection_map_set(VALUE o, VALUE key, VALUE value) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  return iodine_minimap_store(&c->map, key, value);
}

static VALUE iodine_connection_map_get(VALUE o, VALUE key) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  VALUE r = iodine_minimap_rb_get(&c->map, key);
  if (!r)
    goto missing_item;
  return r;

missing_item:
  r = Qnil;
  if (!c->http || !RB_TYPE_P(key, RUBY_T_STRING))
    return r;
  if (1 < fio_http_request_header_count(
              c->http,
              (fio_str_info_s)IODINE_RSTR_INFO(key))) { /* more than one */
    r = rb_ary_new();
    iodine_connection_map_set(o, key, r);
    for (size_t i = 0;; ++i) {
      fio_str_info_s h =
          fio_http_request_header(c->http,
                                  (fio_str_info_s)IODINE_RSTR_INFO(key),
                                  i);
      if (!h.buf)
        break;
      rb_ary_push(r, rb_str_new(h.buf, h.len));
    }
  } else {
    fio_str_info_s h =
        fio_http_request_header(c->http,
                                (fio_str_info_s)IODINE_RSTR_INFO(key),
                                0);
    if (h.buf)
      r = rb_str_new(h.buf, h.len);
    iodine_connection_map_set(o, key, r);
  }
  return r;
}
static VALUE iodine_connection_map_each(VALUE o) {
  iodine_connection_map_headers(o);
  return iodine_minimap_rb_each(iodine_connection_map_ptr(o));
}
static VALUE iodine_connection_map_count(VALUE o) {
  return ULL2NUM((unsigned long long)iodine_hmap_count(
      &iodine_connection_map_ptr(o)->map));
}
FIO_SFUNC VALUE iodine_connection_map_capa(VALUE o) {
  return ULL2NUM(
      (unsigned long long)iodine_hmap_capa(&iodine_connection_map_ptr(o)->map));
}

FIO_SFUNC VALUE iodine_connection_map_reserve(VALUE o, VALUE s_) {
  rb_check_type(s_, T_FIXNUM);
  size_t s = (size_t)NUM2ULL(s_);
  if (s > 0x0FFFFFFFULL)
    rb_raise(
        rb_eRangeError,
        "cannot reserve negative values or values higher than 268,435,455.");
  iodine_minimap_s *m = iodine_connection_map_ptr(o);
  iodine_hmap_reserve(&m->map, s);
  return o;
}

/* *****************************************************************************
HTTP IO Callbacks and Helpers
***************************************************************************** */

/** Callback for HTTP requests (server) or responses (client). */
static void iodine_io_http_on_http(fio_http_s *h);
/** (optional) the callback to be performed when the HTTP service closes. */
static void iodine_io_http_on_stop(struct fio_http_settings_s *settings);

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
/** Called when a request / response cycle is finished with no Upgrade. */
static void iodine_io_http_on_finish(fio_http_s *h);

/* main argument structure */
typedef struct {
  fio_buf_info_s url;
  fio_url_s url_data;
  VALUE rb_tls;
  fio_buf_info_s hint;
  VALUE headers;
  VALUE cookies;
  fio_buf_info_s method;
  fio_buf_info_s body;
  fio_http_settings_s settings;
  enum {
    IODINE_SERVICE_RAW,
    IODINE_SERVICE_HTTP,
    IODINE_SERVICE_WS,
    IODINE_SERVICE_UNKNOWN,
  } service;
} iodine_connection_args_s;

static void iodine_tcp_on_stop(fio_io_protocol_s *p, void *udata);
static void *iodine_tcp_listen(iodine_connection_args_s args);

/* *****************************************************************************
HTTP Cookies
***************************************************************************** */

FIO_SFUNC VALUE iodine_connection_cookie_get(VALUE o, VALUE name) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http) {
    rb_raise(rb_eTypeError,
             "Iodine::Connection instance should be an HTTP connection to "
             "use cookies.");
  }
  if (RB_TYPE_P(name, RUBY_T_SYMBOL))
    name = rb_sym_to_s(name);
  rb_check_type(name, RUBY_T_STRING);
  fio_str_info_s str =
      fio_http_cookie(c->http, RSTRING_PTR(name), RSTRING_LEN(name));
  if (!str.buf)
    return Qnil;
  return rb_usascii_str_new(str.buf, str.len);
}

FIO_SFUNC VALUE iodine_connection_cookie_set(int argc, VALUE *argv, VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http) {
    rb_raise(rb_eTypeError,
             "Iodine::Connection instance should be an HTTP connection to "
             "use cookies.");
  }
  fio_str_info_s name = FIO_STR_INFO0;
  fio_str_info_s value = FIO_STR_INFO0;
  size_t max_age = 0;
  fio_str_info_s domain = FIO_STR_INFO0;
  fio_str_info_s path = FIO_STR_INFO0;
  VALUE same_site_rb = Qnil;
  uint8_t secure = 0;
  uint8_t http_only = 0;
  uint8_t partitioned = 0;

  fio_http_cookie_same_site_e same_site_c =
      FIO_HTTP_COOKIE_SAME_SITE_BROWSER_DEFAULT;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_STR(name, 0, "name", 1),
                  IODINE_ARG_STR(value, 0, "value", 0),
                  IODINE_ARG_SIZE_T(max_age, 0, "max_age", 0),
                  IODINE_ARG_STR(domain, 0, "domain", 0),
                  IODINE_ARG_STR(path, 0, "path", 0),
                  IODINE_ARG_RB(same_site_rb, 0, "same_site", 0),
                  IODINE_ARG_BOOL(secure, 0, "secure", 0),
                  IODINE_ARG_BOOL(http_only, 0, "http_only", 0),
                  IODINE_ARG_BOOL(partitioned, 0, "partitioned", 0));

  if (!name.len)
    return Qnil;
  if (same_site_rb != Qnil) {
    rb_check_type(same_site_rb, RUBY_T_SYMBOL);
    ID same_site_id = rb_sym2id(same_site_rb);
    if (same_site_id == IODINE_SAME_SITE_DEFAULT)
      same_site_c = FIO_HTTP_COOKIE_SAME_SITE_BROWSER_DEFAULT;
    else if (same_site_id == IODINE_SAME_SITE_NONE)
      same_site_c = FIO_HTTP_COOKIE_SAME_SITE_NONE;
    else if (same_site_id == IODINE_SAME_SITE_LAX)
      same_site_c = FIO_HTTP_COOKIE_SAME_SITE_LAX;
    else if (same_site_id == IODINE_SAME_SITE_STRICT)
      same_site_c = FIO_HTTP_COOKIE_SAME_SITE_STRICT;
  }
  if (fio_http_cookie_set(c->http,
                          .name = name,
                          .value = value,
                          .domain = domain,
                          .path = path,
                          .max_age = (int)max_age,
                          .same_site = (fio_http_cookie_same_site_e)same_site_c,
                          .secure = secure,
                          .http_only = http_only,
                          .partitioned = partitioned)) {
    return Qfalse;
    rb_raise(rb_eRuntimeError,
             "cookies cannot be set at this point, please check if headers "
             "were sent before setting a cookie.");
  }
  return Qtrue;
}

static int iodine_connection_cookie_each_task(fio_http_s *h,
                                              fio_str_info_s name,
                                              fio_str_info_s value,
                                              void *info) {
  VALUE *argv = (VALUE *)info;
  ++argv;
  argv[0] = rb_usascii_str_new(name.buf, name.len);
  STORE.hold(argv[0]); /* TODO, avoid STORE for fast path if possible */
  argv[1] = rb_usascii_str_new(value.buf, value.len);
  STORE.hold(argv[1]);
  rb_yield_values2(2, argv);
  STORE.release(argv[1]);
  STORE.release(argv[0]);
  argv[0] = Qnil;
  argv[1] = Qnil;
  return 0;
  (void)h;
}

FIO_SFUNC VALUE iodine_connection_cookie_each_caller(VALUE info_) {
  VALUE *info = (VALUE *)info_;
  iodine_connection_s *c = iodine_connection_ptr(info[0]);
  fio_http_cookie_each(c->http, iodine_connection_cookie_each_task, info);
  return info_;
}

FIO_SFUNC VALUE iodine_connection_cookie_each(VALUE o) {
  rb_block_given_p();
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http) {
    rb_raise(rb_eTypeError,
             "Iodine::Connection instance should be an HTTP connection to "
             "use cookies.");
  }
  VALUE info[3] = {o, Qnil, Qnil};
  int state = 0;
  rb_protect(iodine_connection_cookie_each_caller, (VALUE)info, &state);
  if (!state)
    return o;
  STORE.release(info[2]);
  STORE.release(info[1]);
  VALUE exc = rb_errinfo();
  if (!rb_obj_is_instance_of(exc, rb_eLocalJumpError))
    iodine_handle_exception(NULL);
  rb_set_errinfo(Qnil);
  return o;
}

/* *****************************************************************************
HTTP Body Access
***************************************************************************** */

FIO_SFUNC VALUE iodine_connection_body_length(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http)
    return ULL2NUM(0);
  return ULL2NUM(fio_http_body_length(c->http));
}

FIO_SFUNC VALUE iodine_connection_body_gets(int argc, VALUE *argv, VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http)
    return Qnil;
  size_t length = (size_t)-1;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_SIZE_T(length, 0, "limit", 0));
  fio_str_info_s s = fio_http_body_read_until(c->http, '\n', length);
  if (!s.len)
    return Qnil;
  return rb_usascii_str_new(s.buf, s.len);
}

FIO_SFUNC VALUE iodine_connection_body_read(int argc, VALUE *argv, VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http)
    return Qnil;
  size_t length = (size_t)-1;
  VALUE rbuf = Qnil;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_SIZE_T(length, 0, "maxlen", 0),
                  IODINE_ARG_RB(rbuf, 0, "out_string", 0));
  if (rbuf != Qnil) {
    Check_Type(rbuf, T_STRING);
    rb_str_set_len(rbuf, 0);
  }
  fio_str_info_s s = fio_http_body_read(c->http, length);
  if (s.len) {
    if (rbuf == Qnil)
      rbuf = rb_usascii_str_new(s.buf, s.len);
    else
      rb_str_cat(rbuf, s.buf, s.len);
  }
  return rbuf;
}

FIO_SFUNC VALUE iodine_connection_body_seek(int argc, VALUE *argv, VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (!c->http)
    return Qnil;
  int64_t pos = 0;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_NUM(pos, 0, "pos", 0));
  pos = (long long)fio_http_body_seek(c->http, (ssize_t)pos);
  return ULL2NUM(pos);
}

/* *****************************************************************************
HTTP Response API TODO

- `write_header(name, value)`: Sets a response header and returns `true`. If
   headers were already sent or either `write` or `finish` was previously
   called, it **MUST** return `false`.

  - The header `name` **MUST** be a lowercase string. Servers **MAY** enforce
    this by converting string objects to lowercase.

  - Servers **MAY** accept Symbols as the header `name` well.

  - `value` **MUST** be either a String or an Array of Strings. Servers **SHOULD
    NOT** (but **MAY**) accept other `value` types.

  - The `write_header` method is **irreversible**. Servers **MAY** write the
    header immediately, as they see fit.

  - When `write_header` **MAY** be called multiple times for the same header
    `name`. This **MAY** result in multiple headers with the same name being
    sent. Servers **SHOULD** avoid sending the same header name if it is
    forbidden by the HTTP standard.

* `write(data)` - **streams** the data, using the appropriate encoding.
**Note**:

    * `data` MUST be either String object or a
      [File](https://ruby-doc.org/core-2.7.0/File.html) (or
      [TempFile](https://ruby-doc.org/stdlib-2.7.0/libdoc/tempfile/rdoc/Tempfile.html))
      object.

    * If `data` is a `File` instance, then the server **MUST** call it's `close`
method after the data was sent.

    * If the `"content-type"` was set to `text/event-stream`, this is an SSE /
EventSource connection and servers **MUST** send the data as is (the encoding
**MUST** be assumed to be handled by the application layer).

    * Otherwise, if the `"content-length"` header wasn't set, the server **MUST
EITHER** use `chunked` [transfer
encoding](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Transfer-Encoding),
**OR** set the [`connection: close`
header](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Connection)
and close the connection once `finish` was called.

    * If the headers weren't previously sent, they **MUST** be sent (or locked)
at this point. Once `write` or `finish` are called, calls to `write_header`
**MUST** return `false`.

* `finish([data])` - completes the response. Note:

    * Subsequent calls to `finish` **MUST** be ignored (except `close` **MUST**
still be called if `data` is a `File` instance).

    * `data` MUST be either `nil`, or a String object, or a
[File](https://ruby-doc.org/core-2.7.0/File.html) (or
[TempFile](https://ruby-doc.org/stdlib-2.7.0/libdoc/tempfile/rdoc/Tempfile.html))
object.

    * If `data` is a `File` instance, then the server **MUST** call it's `close`
method after the data was sent.

    * If the headers weren't previously sent, they **MUST** be sent before
sending any data.

    * If `data` was provided, it should be sent. If no previous calls to `write`
were made and no `"content-length"` header was set, the server **MAY** set the
`"content-length"` for the response before sending the `data`.

* `headers_sent?` - returns `true` if additional headers cannot be sent (the
headers were already sent). Otherwise returns `false`. Servers **MAY** return
`false` **even if** the response is implemented using `chunked` encoding with
trailers, allowing certain headers to be sent after the response was sent.

* `valid?` - returns `true` if data may still be sent (the connection is open
and `finish` hadn't been called yet). Otherwise returns `false`.

* `dup` - (optional) **SHOULD** throw an exception, as the `event` object **MUST
NOT** be duplicated by the NeoRack Application.

***************************************************************************** */

/* *****************************************************************************
Lower-Case Helper for Header Names
***************************************************************************** */

#define IODINE___COPY_TO_LOWER_CASE(var_name, inpute_var)                      \
  FIO_STR_INFO_TMP_VAR(var_name, 4096);                                        \
  iodine___copy_to_lower_case(&var_name, &inpute_var);

/** Converts a Header key to lower-case */
FIO_IFUNC void iodine___copy_to_lower_case(fio_str_info_s *t,
                                           fio_str_info_s *k) {
  if (k->len >= t->capa)
    goto too_big;
  for (size_t i = 0; i < k->len; ++i) {
    uint8_t c = (uint8_t)k->buf[i];
    c |= (uint8_t)(c >= 'A' || c <= 'Z') << 5;
    t->buf[i] = c;
  }
  t->len = k->len;
  return;
too_big:
  *t = *k;
}

/* *****************************************************************************
Default Handler Implementations
***************************************************************************** */

static VALUE iodine_handler_deafult_on_http404(VALUE handler, VALUE client) {
  iodine_connection_s *c = iodine_connection_ptr(client);
  if (!c->http)
    return Qnil;
  fio_http_send_error_response(c->http, 404);
  return Qnil;
  (void)handler;
}

static VALUE iodine_handler_deafult_on_event(VALUE handler, VALUE client) {
  return Qnil;
  (void)handler, (void)client;
}

static VALUE iodine_handler_deafult_on_auth(VALUE handler, VALUE client) {
  return Qtrue;
  (void)handler, (void)client;
}
static VALUE iodine_handler_deafult_on_authx(VALUE handler, VALUE client) {
  return Qfalse;
  (void)handler, (void)client;
}

static VALUE iodine_handler_on_auth_reroute(VALUE handler, VALUE client) {
  iodine_caller_result_s r =
      iodine_ruby_call_inside(handler, IODINE_ON_AUTHENTICATE_ID, 1, &client);
  if (r.exception || r.result != Qtrue)
    return Qfalse;
  return Qtrue;
}

static VALUE iodine_handler_deafult_on_message(VALUE h, VALUE c, VALUE m) {
  return Qnil;
  (void)h, (void)c, (void)m;
}

static void iodine_connection___add_header(fio_http_s *h,
                                           fio_str_info_s n,
                                           fio_str_info_s v) {
  char *eol = (char *)memchr(v.buf, '\n', v.len);
  while (eol) {
    ++eol;
    fio_str_info_s tmp = FIO_STR_INFO2(v.buf, (size_t)(eol - v.buf));
    fio_http_response_header_add(h, n, tmp);
    v.buf = eol;
    v.len -= tmp.len;
  }
  fio_http_response_header_add(h, n, v);
}

static int iodine_handler_deafult_on_http__header2(fio_str_info_s n,
                                                   VALUE v,
                                                   VALUE h_) {
  fio_http_s *h = (fio_http_s *)h_;
  FIO_STR_INFO_TMP_VAR(vstr, 24);
  if (RB_TYPE_P(v, RUBY_T_ARRAY))
    goto is_array;
  if (RB_TYPE_P(v, RUBY_T_SYMBOL))
    v = rb_sym_to_s(v);
  if (RB_TYPE_P(v, RUBY_T_STRING))
    vstr = (fio_str_info_s)IODINE_RSTR_INFO(v);
  else if (RB_TYPE_P(v, RUBY_T_FIXNUM))
    fio_string_write_i(&vstr, NULL, (int64_t)RB_NUM2LL(v));
  else
    goto type_error;
  iodine_connection___add_header(h, n, vstr);
  return ST_CONTINUE;

is_array:
  for (size_t i = 0; i < (size_t)RARRAY_LEN(v); ++i) {
    VALUE t = RARRAY_PTR(v)[i];
    if (!RB_TYPE_P(t, RUBY_T_STRING))
      continue;
    iodine_connection___add_header(h, n, (fio_str_info_s)IODINE_RSTR_INFO(t));
  }
  return ST_CONTINUE;

type_error:
  FIO_LOG_WARNING("Rack response contained an invalid header value type. "
                  "Header values MUST be Strings (or Arrays of Strings)");
  return ST_CONTINUE;
}

static int iodine_handler_deafult_on_http__header(VALUE n_, VALUE v, VALUE h_) {
  fio_str_info_s header_rb;
  if (RB_TYPE_P(n_, RUBY_T_SYMBOL))
    n_ = rb_sym_to_s(n_);
  if (!RB_TYPE_P(n_, RUBY_T_STRING))
    return ST_CONTINUE;
  header_rb = (fio_str_info_s)IODINE_RSTR_INFO(n_);
  IODINE___COPY_TO_LOWER_CASE(n, header_rb);
  if (n.len > 5 && n.buf[4] == '.' && /* ignore "rack.<name>" headers */
      fio_buf2u32u("rack") == fio_buf2u32u(n.buf))
    return ST_CONTINUE;
  return iodine_handler_deafult_on_http__header2(n, v, h_);
}

typedef struct {
  fio_http_s *h;
  char *out;
} iodine_body_each_info_s;

static VALUE iodine_handler_deafult_on_http__each_body(VALUE s, VALUE info_) {
  iodine_body_each_info_s *i = (iodine_body_each_info_s *)info_;
  if (!RB_TYPE_P(s, RUBY_T_STRING))
    s = rb_any_to_s(s);
  if (!RB_TYPE_P(s, RUBY_T_STRING))
    goto error_in_type;
  i->out = fio_bstr_write(i->out, RSTRING_PTR(s), (size_t)RSTRING_LEN(s));
  // fio_http_write(h,
  //                .buf = RSTRING_PTR(s),
  //                .len = (size_t)RSTRING_LEN(s),
  //                .copy = 1,
  //                .finish = 0);
  return s;

error_in_type:
  if (s != Qnil)
    FIO_LOG_ERROR(
        "HTTP response body .each must be yield only String Objects.");
  return s;
}

static VALUE iodine_connection_env_get(VALUE self);
static VALUE iodine_connection_handler_set(VALUE client, VALUE handler);
static VALUE iodine_connection_rack_hijack(VALUE self);
static int iodine_connection_rack_hijack_partial(iodine_connection_s *c,
                                                 VALUE proc);
static VALUE iodine_connection_peer_addr(VALUE self);
static VALUE iodine_handler_deafult_on_http(VALUE handler, VALUE client) {
  /* RACK specification: https://github.com/rack/rack/blob/main/SPEC.rdoc */
  VALUE returned_value = Qnil;
  VALUE partial_hijack = Qnil;
  iodine_connection_s *c = iodine_connection_ptr(client);
  if (!c->http)
    return returned_value;
  VALUE r, env;
  bool should_finish = 1;
  /* collect `env` and call `call`. */
  env = iodine_connection_env_get(client);
  /* call `call` from top of MiddleWare / App chain */
  r = c->store[IODINE_CONNECTION_STORE_rack] =
      rb_funcallv(c->store[IODINE_CONNECTION_STORE_handler],
                  IODINE_CALL_ID,
                  1,
                  &env);
  if (!c->http)
    return returned_value;
  if (!RB_TYPE_P(r, RUBY_T_ARRAY))
    goto rack_error;
  if (RARRAY_LEN(r) < 3)
    goto rack_error;
  { /* handle status */
    VALUE s = RARRAY_PTR(r)[0];
    long long i = 0;
    switch (rb_type(s)) {
    case RUBY_T_STRING: i = rb_str_to_inum(s, 10, 0); break;
    case RUBY_T_FIXNUM: i = NUM2LL(s); break;
    case RUBY_T_TRUE: i = 200; break;
    case RUBY_T_FALSE: i = 404; break;
    default: i = 0;
    }
    if (i > 0 && i < 1000)
      fio_http_status_set(c->http, (size_t)i);
  }
  { /* handle headers */
    VALUE hdr = RARRAY_PTR(r)[1];
    if (RB_TYPE_P(hdr, RUBY_T_HASH)) {
      partial_hijack = rb_hash_delete(hdr, IODINE_RACK_HIJACK_STR);
      should_finish = (partial_hijack == Qnil);
      rb_hash_foreach(hdr,
                      iodine_handler_deafult_on_http__header,
                      (VALUE)c->http);
    } else if (RB_TYPE_P(hdr, RUBY_T_ARRAY)) {
      for (size_t i = 0; i < (size_t)RARRAY_LEN(hdr); ++i) {
        VALUE t = RARRAY_PTR(hdr)[i];
        if (!RB_TYPE_P(t, RUBY_T_ARRAY) || RARRAY_LEN(t) != 2 ||
            !RB_TYPE_P(RARRAY_PTR(t)[0], RUBY_T_STRING))
          goto rack_error;
        fio_str_info_s hn = (fio_str_info_s)IODINE_RSTR_INFO(RARRAY_PTR(t)[0]);
        if (!FIO_STR_INFO_IS_EQ(
                (fio_buf_info_s)IODINE_RSTR_INFO(RARRAY_PTR(t)[0]),
                (fio_buf_info_s)IODINE_RSTR_INFO(IODINE_RACK_HIJACK_STR))) {
          iodine_connection___add_header(
              c->http,
              hn,
              (fio_str_info_s)IODINE_RSTR_INFO(RARRAY_PTR(t)[1]));
          continue;
        }
        partial_hijack = RARRAY_PTR(t)[1];
        should_finish = (partial_hijack == Qnil);
      }
    } else {
      FIO_LOG_ERROR("Rack application response headers type error");
      goto rack_error;
    }
  }
  if (!(c->flags & IODINE_CONNECTION_UPGRADE)) { /* handle body */
    VALUE bd = RARRAY_PTR(r)[2];
    if (rb_respond_to(bd, IODINE_EACH_ID)) {      /* enumerable body... */
      if (rb_respond_to(bd, IODINE_TO_PATH_ID)) { /* named file body... */
        VALUE p = rb_funcallv(bd, IODINE_TO_PATH_ID, 0, NULL);
        if (!RB_TYPE_P(p, RUBY_T_STRING))
          goto rack_error; /* FIXME? something else? */
        if (fio_http_static_file_response(c->http,
                                          (fio_str_info_s)IODINE_RSTR_INFO(p),
                                          FIO_STR_INFO0,
                                          fio_http_settings(c->http)->max_age))
          goto rack_error; /* FIXME? something else? */
      } else {
        iodine_body_each_info_s each = {.h = c->http};
        rb_block_call(
            bd,
            IODINE_EACH_ID,
            0,
            NULL,
            (rb_block_call_func_t)iodine_handler_deafult_on_http__each_body,
            (VALUE)&each);
        fio_http_write(c->http,
                       .buf = each.out,
                       .len = fio_bstr_len(each.out),
                       .dealloc = (void (*)(void *))fio_bstr_free,
                       .finish = should_finish);
      }
    } else if (RB_TYPE_P(bd, RUBY_T_STRING)) { /* a simple String */
      fio_http_write(c->http,
                     .buf = RSTRING_PTR(bd),
                     .len = (size_t)RSTRING_LEN(bd),
                     .copy = 1,
                     .finish = should_finish);
    } else if (bd == Qnil) { /* do nothing? no body. */
      fio_http_write(c->http, .finish = should_finish);
    } else { /* streaming body – answers to `call` */
      partial_hijack = bd;
    }
  }
  if (partial_hijack && partial_hijack != Qnil &&
      iodine_connection_rack_hijack_partial(c, partial_hijack))
    goto rack_error;

  rb_check_funcall(RARRAY_PTR(r)[2], IODINE_CLOSE_ID, 0, NULL);

after_reply:

  r = rb_hash_aref(env, IODINE_RACK_AFTER_RPLY_STR);
  if (RB_TYPE_P(r, RUBY_T_ARRAY))
    for (size_t i = 0; i < (size_t)RARRAY_LEN(r); ++i) {
      // rb_funcallv(RARRAY_PTR(r)[i], IODINE_CALL_ID, 0, NULL);
      IODINE_DEFER_BLOCK(RARRAY_PTR(r)[i]);
    }

  c->store[IODINE_CONNECTION_STORE_rack] = Qnil;
  return returned_value;

rack_error:
  if (RB_TYPE_P(r, RUBY_T_ARRAY) && RARRAY_LEN(r) >= 3)
    rb_check_funcall(RARRAY_PTR(r)[2], IODINE_CLOSE_ID, 0, NULL);
  fio_http_send_error_response(c->http, 500);
  c->store[IODINE_CONNECTION_STORE_rack] = Qnil;
  goto after_reply;
  (void)handler;
}

static VALUE iodine_handler_on_auth_rack_internal(VALUE handler,
                                                  VALUE client,
                                                  VALUE upgrd_sym) {
  VALUE env = iodine_connection_env_get(client);
  rb_hash_aset(env, IODINE_RACK_UPGRADE_Q_STR, upgrd_sym);
  iodine_handler_deafult_on_http(handler, client);
  /* test old-style iodine Upgrade approach */
  VALUE hn = rb_hash_aref(env, IODINE_RACK_UPGRADE_STR);
  if (hn != Qnil) {
    iodine_connection_s *c = iodine_connection_ptr(client);
    if (hn != Qtrue && hn != c->store[IODINE_CONNECTION_STORE_handler])
      iodine_connection_handler_set(client, hn);
    return Qtrue;
  }
  return Qnil;
}

static VALUE iodine_handler_on_auth_rack_ws(VALUE handler, VALUE client) {
  return iodine_handler_on_auth_rack_internal(handler,
                                              client,
                                              IODINE_RACK_UPGRADE_WS_SYM);
}
static VALUE iodine_handler_on_auth_rack_sse(VALUE handler, VALUE client) {
  return iodine_handler_on_auth_rack_internal(handler,
                                              client,
                                              IODINE_RACK_UPGRADE_SSE_SYM);
}

static VALUE iodine_handler_method_injection__inner(VALUE self,
                                                    VALUE handler,
                                                    bool is_middleware) {
  static VALUE previous = Qnil;
  if (handler == previous)
    return handler;
  previous = handler;
/* test for handler responses and set a callback if missing */
#define IODINE_DEFINE_MISSING_CALLBACK(id)                                     \
  do {                                                                         \
    if (!rb_respond_to(handler, id))                                           \
      rb_define_singleton_method(handler,                                      \
                                 rb_id2name(id),                               \
                                 iodine_handler_deafult_on_event,              \
                                 1);                                           \
  } while (0)
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_FINISH_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_CLOSE_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_EVENTSOURCE_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_EVENTSOURCE_RECONNECT_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_DRAINED_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_SHUTDOWN_ID);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_TIMEOUT_ID);
#undef IODINE_DEFINE_MISSING_CALLBACK

#define IODINE_DEFINE_MISSING_CALLBACK(id, callback, nargs)                    \
  do {                                                                         \
    if (!rb_respond_to(handler, id))                                           \
      rb_define_singleton_method(handler, rb_id2name(id), callback, nargs);    \
  } while (0)

  /* Has `call` but not `on_http`? assume Rack. */
  if (rb_respond_to(handler, IODINE_CALL_ID) &&
      !rb_respond_to(handler, IODINE_ON_HTTP_ID)) {
    rb_define_singleton_method(handler,
                               "on_http",
                               iodine_handler_deafult_on_http,
                               1);
    IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_SSE_ID,
                                   iodine_handler_on_auth_rack_sse,
                                   1);
    IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_WEBSOCKET_ID,
                                   iodine_handler_on_auth_rack_ws,
                                   1);
  } else {
    IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_HTTP_ID,
                                   iodine_handler_deafult_on_http404,
                                   1);
  }

  if (rb_respond_to(handler, IODINE_ON_AUTHENTICATE_ID)) {
    IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_SSE_ID,
                                   iodine_handler_on_auth_reroute,
                                   1);
    IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_WEBSOCKET_ID,
                                   iodine_handler_on_auth_reroute,
                                   1);
  } else {
    _Bool responds = rb_respond_to(handler, IODINE_ON_OPEN_ID);
    IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_SSE_ID,
                                   (responds ? iodine_handler_deafult_on_auth
                                             : iodine_handler_deafult_on_authx),
                                   1);
    responds |= rb_respond_to(handler, IODINE_ON_MESSAGE_ID);
    IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_AUTHENTICATE_WEBSOCKET_ID,
                                   (responds ? iodine_handler_deafult_on_auth
                                             : iodine_handler_deafult_on_authx),
                                   1);
  }

  /* should be defined last, as its existence controls behavior */
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_OPEN_ID,
                                 iodine_handler_deafult_on_event,
                                 1);
  IODINE_DEFINE_MISSING_CALLBACK(IODINE_ON_MESSAGE_ID,
                                 iodine_handler_deafult_on_message,
                                 2);
#undef IODINE_DEFINE_MISSING_CALLBACK
  return handler;
  (void)self;
}

static VALUE iodine_handler_method_injection(VALUE self, VALUE handler) {
  return iodine_handler_method_injection__inner(self, handler, 0);
}

/* *****************************************************************************
Handler Set function
***************************************************************************** */

/** Sets the client's handler object, adding default callback methods if
 * missing. */
static VALUE iodine_connection_handler_set(VALUE client, VALUE handler) {
  iodine_connection_s *c = iodine_connection_ptr(client);
  if (!c)
    return Qnil;
  if (IODINE_STORE_IS_SKIP(handler))
    rb_raise(rb_eArgError, "invalid handler!");
  iodine_handler_method_injection(Qnil, handler);
  c->store[IODINE_CONNECTION_STORE_handler] = handler;
  return handler;
}

/* *****************************************************************************
ENV get / conversion
***************************************************************************** */

static int iodine_env_populate_header_data(fio_http_s *h,
                                           fio_str_info_s n,
                                           fio_str_info_s v,
                                           void *c_) {
  iodine_connection_s *c = (iodine_connection_s *)c_;
  VALUE key = STORE.header_name(n);
  c->store[IODINE_CONNECTION_STORE_tmp] = key;
  /* copy value */
  VALUE val = rb_str_new(v.buf, v.len);
  c->store[IODINE_CONNECTION_STORE_rack] = val; /* unused at this point */
  /* finish up */
  rb_hash_aset(c->store[IODINE_CONNECTION_STORE_env], key, val);
  c->store[IODINE_CONNECTION_STORE_rack] = Qnil;
  c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  return 0;
  (void)h;
}

static void iodine_env_set_key_pair(VALUE env,
                                    fio_str_info_s n,
                                    fio_str_info_s v) {
  VALUE key = STORE.frozen_str(n);
  STORE.hold(key);
  VALUE val = rb_str_new(v.buf, v.len);
  STORE.hold(val);
  rb_hash_aset(env, key, val);
  STORE.release(val);
  STORE.release(key);
}

static void iodine_env_set_key_pair_const(VALUE env,
                                          fio_str_info_s n,
                                          fio_str_info_s v) {
  VALUE key = STORE.frozen_str(n);
  STORE.hold(key);
  VALUE val = STORE.frozen_str(v);
  STORE.hold(val);
  rb_hash_aset(env, key, val);
  STORE.release(val);
  STORE.release(key);
}

static void iodine_env_set_const_val(VALUE env, fio_str_info_s n, VALUE val) {
  VALUE key = STORE.frozen_str(n);
  STORE.hold(key);
  rb_hash_aset(env, key, val);
  STORE.release(key);
}

static void iodine_connection_init_env_template(fio_buf_info_s at_url) {
  VALUE env = IODINE_CONNECTION_ENV_TEMPLATE = rb_hash_new();
  STORE.hold(IODINE_CONNECTION_ENV_TEMPLATE);
  /* set template, see https://github.com/rack/rack/blob/main/SPEC.rdoc */
  // TODO: REMOTE_ADDR
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.multithread"),
                           (fio_cli_get_i("-t") ? Qtrue : Qfalse));
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.multiprocess"),
                           (fio_cli_get_i("-w") ? Qtrue : Qfalse));
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"rack.run_once"), Qfalse);
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.errors"),
                           rb_stderr);
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"rack.multipart.buffer_size"),
                           Qnil);
  iodine_env_set_const_val(
      env,
      FIO_STR_INFO1((char *)"rack.multipart.tempfile_factory"),
      Qnil);
  iodine_env_set_const_val(
      env,
      FIO_STR_INFO1((char *)"rack.logger"),
      rb_funcallv(rb_const_get(rb_cObject, rb_intern2("Logger", 6)),
                  IODINE_NEW_ID,
                  1,
                  &rb_stderr));
  iodine_env_set_key_pair(
      env,
      FIO_STR_INFO2((char *)"rack.url_scheme", 15),
      FIO_STR_INFO2((char *)"https",
                    (fio_cli_get("-tls") ? 5 : 4))); /* FIXME? */

  rb_hash_aset(env, IODINE_RACK_AFTER_RPLY_STR, Qnil);

  iodine_env_set_const_val(
      env,
      FIO_STR_INFO1((char *)"rack.input"),
      rb_funcallv(rb_const_get(rb_cObject, rb_intern2("StringIO", 8)),
                  IODINE_NEW_ID,
                  0,
                  NULL));
  /* FIXME! TODO! */
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"rack.session"), Qnil);
  {
    VALUE ver = rb_ary_new();
    iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"rack.version"), ver);
    rb_ary_push(ver, INT2NUM(1));
    rb_ary_push(ver, INT2NUM(3));
  }
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"rack.hijack?"), Qtrue);
  rb_hash_aset(env, IODINE_RACK_HIJACK_STR, Qnil);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"neorack.client"), Qnil);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"REQUEST_METHOD"), Qtrue);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"PATH_INFO"), Qtrue);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"QUERY_STRING"), Qtrue);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"REMOTE_ADDR"), Qnil);
  iodine_env_set_const_val(env,
                           FIO_STR_INFO1((char *)"SERVER_PROTOCOL"),
                           Qtrue);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"HTTP_VERSION"), Qtrue);
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO1((char *)"SERVER_NAME"),
                          FIO_STR_INFO0);
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO1((char *)"SCRIPT_NAME"),
                          FIO_STR_INFO0);
  { /* SERVER_PORT must be a String Object. */
    uint64_t port_num = 3000;
    uint8_t was_set = 0;
    if (at_url.len) {
      fio_url_s u = fio_url_parse(at_url.buf, at_url.len);
      port_num = u.port.len ? fio_atol(&u.port.buf) : 0;
    } else {
      if (fio_cli_get_i("-p")) {
        port_num = fio_cli_get_i("-p");
        was_set = 1;
      } else if (fio_cli_get("-b")) {
        fio_url_s u =
            fio_url_parse(fio_cli_get("-b"), strlen(fio_cli_get("-b")));
        if (u.port.len) {
          port_num = fio_atol(&u.port.buf);
          was_set = 1;
        } else if (u.host.len) {
          port_num = 80;
          if (fio_url_is_tls(u).tls)
            port_num = 443;
          was_set = 1;
        }
      }
      if (!was_set && getenv("PORT") && strlen(getenv("PORT"))) {
        char *tmp = getenv("PORT");
        port_num = fio_atol(&tmp);
        was_set = 1;
      }
    }
    FIO_STR_INFO_TMP_VAR(prt, 32);
    fio_string_write_u(&prt, NULL, port_num);
    iodine_env_set_key_pair_const(env,
                                  FIO_STR_INFO1((char *)"SERVER_PORT"),
                                  prt);
  }
}

/** Returns the client's current `env` object. */
static VALUE iodine_connection_env_get(VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (!c)
    return Qnil;
  if (c->store[IODINE_CONNECTION_STORE_env] != Qnil)
    return c->store[IODINE_CONNECTION_STORE_env];
  if (!c->http)
    return (c->store[IODINE_CONNECTION_STORE_env] = rb_hash_new());
  VALUE env = c->store[IODINE_CONNECTION_STORE_env] =
      rb_hash_dup(IODINE_CONNECTION_ENV_TEMPLATE);
  iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"neorack.client"), self);
  {
    VALUE tmp = rb_obj_method(self, IODINE_RACK_HIJACK_ID_SYM);
    c->store[IODINE_CONNECTION_STORE_tmp] = tmp;
    rb_hash_aset(env, IODINE_RACK_HIJACK_STR, tmp);
    c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  }

  /* populate env, see https://github.com/rack/rack/blob/main/SPEC.rdoc */
  fio_http_request_header_each(c->http,
                               iodine_env_populate_header_data,
                               (void *)c);

  if (fio_http_body_length(c->http)) {
    FIO_STR_INFO_TMP_VAR(num2str, 512);
    fio_string_write_u(&num2str, NULL, fio_http_body_length(c->http));
    VALUE clen_str = rb_str_new(num2str.buf, num2str.len);
    c->store[IODINE_CONNECTION_STORE_tmp] = clen_str;
    rb_hash_aset(env,
                 STORE.header_name(FIO_STR_INFO1((char *)"content-length")),
                 clen_str);
    c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
    iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"rack.input"), self);
  }

  iodine_env_set_key_pair_const(env,
                                FIO_STR_INFO2((char *)"REQUEST_METHOD", 14),
                                fio_keystr_info(&c->http->method));
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO2((char *)"PATH_INFO", 9),
                          fio_keystr_info(&c->http->path));
  iodine_env_set_key_pair(env,
                          FIO_STR_INFO2((char *)"QUERY_STRING", 12),
                          fio_keystr_info(&c->http->query));
  {
    fio_str_info_s host =
        fio_http_request_header(c->http, FIO_STR_INFO2((char *)"host", 4), 0);
    fio_url_s u = fio_url_parse(host.buf, host.len);
    iodine_env_set_key_pair_const(env,
                                  FIO_STR_INFO2((char *)"SERVER_NAME", 11),
                                  FIO_BUF2STR_INFO(u.host));
  }
  iodine_env_set_key_pair_const(env,
                                FIO_STR_INFO2((char *)"SERVER_PROTOCOL", 15),
                                fio_keystr_info(&c->http->version));
  iodine_env_set_key_pair_const(env,
                                FIO_STR_INFO2((char *)"HTTP_VERSION", 12),
                                fio_keystr_info(&c->http->version));
  {
    VALUE addr = iodine_connection_peer_addr(self);
    c->store[IODINE_CONNECTION_STORE_tmp] = addr;
    iodine_env_set_const_val(env, FIO_STR_INFO1((char *)"REMOTE_ADDR"), addr);
    c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  }
  {
    VALUE ary_after = rb_ary_new();
    c->store[IODINE_CONNECTION_STORE_tmp] = ary_after;
    rb_hash_aset(env, IODINE_RACK_AFTER_RPLY_STR, ary_after);
    c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  }

  return env;
}

/* *****************************************************************************
Raw IO Callbacks and Helpers (TCP/IP)
***************************************************************************** */

typedef struct {
  fio_io_s *io;
  size_t len;
  char buf[IODINE_RAW_ON_DATA_READ_BUFFER];
} iodine_io_raw_on_data_info_s;

static void *iodine_io_raw_on_attach_in_GIL(void *io) {
  VALUE connection = iodine_connection_create_from_io(io);
  if (!connection || connection == Qnil)
    return NULL;
  iodine_connection_s *c = iodine_connection_ptr(connection);
  iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],
                          IODINE_ON_OPEN_ID,
                          1,
                          &connection);
  return NULL;
}

/** Called when an IO is attached to the protocol. */
static void iodine_io_raw_on_attach(fio_io_s *io) {
  /* Enter GIL */
  rb_thread_call_with_gvl(iodine_io_raw_on_attach_in_GIL, io);
}

static void *iodine_io_raw_on_data_in_GVL(void *info_) {
  iodine_io_raw_on_data_info_s *i = (iodine_io_raw_on_data_info_s *)info_;
  VALUE connection = (VALUE)fio_io_udata(i->io);
  if (!connection || connection == Qnil)
    return NULL;
  iodine_connection_s *c = iodine_connection_ptr(connection);
  VALUE buf = rb_usascii_str_new(i->buf, (long)i->len);
  c->store[IODINE_CONNECTION_STORE_tmp] = buf;
  VALUE args[] = {connection, buf};
  iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],
                          IODINE_ON_MESSAGE_ID,
                          2,
                          args);
  c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  return NULL;
}

static void iodine_io_raw_on_data(fio_io_s *io) {
  iodine_io_raw_on_data_info_s i;
  i.io = io;
  i.len = fio_io_read(io, i.buf, IODINE_RAW_ON_DATA_READ_BUFFER);
  if (!i.len)
    return;
  rb_thread_call_with_gvl(iodine_io_raw_on_data_in_GVL, &i);
}

#define IODINE_CONNECTION_DEF_CB(named, id)                                    \
  static void iodine_io_raw_##named(fio_io_s *io) {                            \
    VALUE connection = (VALUE)fio_io_udata(io);                                \
    if (!connection || connection == Qnil)                                     \
      return;                                                                  \
    iodine_connection_s *c = iodine_connection_ptr(connection);                \
    if (!c)                                                                    \
      return;                                                                  \
    iodine_ruby_call_outside(c->store[IODINE_CONNECTION_STORE_handler],        \
                             id,                                               \
                             1,                                                \
                             &connection);                                     \
  }

/** called once all pending `write` calls are finished. */
IODINE_CONNECTION_DEF_CB(on_ready, IODINE_ON_DRAINED_ID);
/** called if the connection is open when the server is shutting down. */
IODINE_CONNECTION_DEF_CB(on_shutdown, IODINE_ON_SHUTDOWN_ID);
/** Called when a connection's open while server is shutting down. */
static void iodine_io_raw_on_shutdown(fio_io_s *io);
/** Called when a connection's timeout was reached */
IODINE_CONNECTION_DEF_CB(on_timeout, IODINE_ON_TIMEOUT_ID);

#undef IODINE_CONNECTION_DEF_CB

/** Called after the connection was closed (called once per IO). */
static void iodine_io_raw_on_close(void *buf, void *udata) {
  VALUE connection = (VALUE)udata;
  if (!connection || connection == Qnil)
    return;
  iodine_connection_s *c = iodine_connection_ptr(connection);
  if (!c)
    return;
  iodine_ruby_call_outside(c->store[IODINE_CONNECTION_STORE_handler],
                           IODINE_ON_CLOSE_ID,
                           1,
                           &connection);
  (void)buf;
}

static fio_io_protocol_s IODINE_RAW_PROTOCOL = {
    .on_attach = iodine_io_raw_on_attach,
    .on_data = iodine_io_raw_on_data,
    .on_ready = iodine_io_raw_on_ready,
    .on_timeout = iodine_io_raw_on_timeout,
    .on_shutdown = iodine_io_raw_on_shutdown,
    .on_close = iodine_io_raw_on_close,
    .on_pubsub = FIO_ON_MESSAGE_SEND_MESSAGE,
};

/* *****************************************************************************
HTTP / WebSockets Callbacks and Helpers
***************************************************************************** */
/** TODO: fix me */

static void *iodine_io_http_on_http_internal(void *h_) {
  fio_http_s *h = (fio_http_s *)h_;
  VALUE connection = iodine_connection_create_from_http(h);
  if (FIO_UNLIKELY(!connection || connection == Qnil)) {
    FIO_LOG_FATAL("`on_http` couldn't allocate Iodine::Connection object!");
    return NULL;
  }
  iodine_connection_s *c = iodine_connection_ptr(connection);
  iodine_caller_result_s e =
      iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],
                              IODINE_ON_HTTP_ID,
                              1,
                              &connection);
  if (e.exception) {
    fio_http_send_error_response(h, 500);
  }
  return NULL;
}

static void iodine_io_http_on_http(fio_http_s *h) {
  VALUE handler = (VALUE)fio_http_udata(h);
  if (FIO_UNLIKELY(!handler || handler == Qnil)) {
    FIO_LOG_FATAL("`on_http` callback couldn't find handler!");
    fio_http_send_error_response(h, 500);
    return;
  }
  rb_thread_call_with_gvl(iodine_io_http_on_http_internal, (void *)h);
}

#define IODINE_CONNECTION_DEF_CB(named, id, free_handle)                       \
  static void iodine_io_http_##named(fio_http_s *h) {                          \
    VALUE connection = (VALUE)fio_http_udata2(h);                              \
    if (!connection || connection == Qnil)                                     \
      return;                                                                  \
    iodine_connection_s *c = iodine_connection_ptr(connection);                \
    if (!c)                                                                    \
      return;                                                                  \
    iodine_ruby_call_outside(c->store[IODINE_CONNECTION_STORE_handler],        \
                             id,                                               \
                             1,                                                \
                             &connection);                                     \
    if (free_handle) {                                                         \
      iodine_ruby_call_outside(c->store[IODINE_CONNECTION_STORE_handler],      \
                               IODINE_ON_FINISH_ID,                            \
                               1,                                              \
                               &connection);                                   \
      fio_http_free(c->http);                                                  \
      c->http = NULL;                                                          \
      c->io = NULL;                                                            \
      STORE.release(connection);                                               \
    }                                                                          \
  }

/** Called when an IO is attached to the protocol. */
IODINE_CONNECTION_DEF_CB(on_open, IODINE_ON_OPEN_ID, 0);
/** called once all pending `write` calls are finished. */
IODINE_CONNECTION_DEF_CB(on_ready, IODINE_ON_DRAINED_ID, 0);
/** called if the connection is open when the server is shutting down. */
IODINE_CONNECTION_DEF_CB(on_shutdown, IODINE_ON_SHUTDOWN_ID, 0);
/** Called after the connection was closed (called once per IO). */
IODINE_CONNECTION_DEF_CB(on_close, IODINE_ON_CLOSE_ID, 1);

#undef IODINE_CONNECTION_DEF_CB
#define IODINE_CONNECTION_DEF_CB(named, id, flag)                              \
  static void *iodine_io_http_##named##_internal(void *h_) {                   \
    fio_http_s *h = (fio_http_s *)h_;                                          \
    VALUE connection = iodine_connection_create_from_http(h);                  \
    if (!connection || connection == Qnil)                                     \
      return (void *)-1;                                                       \
    iodine_connection_s *c = iodine_connection_ptr(connection);                \
    if (!c)                                                                    \
      return (void *)-1;                                                       \
    c->flags |= flag;                                                          \
    iodine_caller_result_s r =                                                 \
        iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],     \
                                id,                                            \
                                1,                                             \
                                &connection);                                  \
    if (!r.exception && r.result == Qtrue)                                     \
      return NULL;                                                             \
    iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],         \
                            IODINE_ON_FINISH_ID,                               \
                            1,                                                 \
                            &connection);                                      \
    fio_http_free(c->http);                                                    \
    c->http = NULL;                                                            \
    c->io = NULL;                                                              \
    return (void *)-1;                                                         \
  }                                                                            \
  static int iodine_io_http_##named(fio_http_s *h) {                           \
    if (!rb_thread_call_with_gvl(iodine_io_http_##named##_internal, h))        \
      return 0;                                                                \
    return -1;                                                                 \
  }

/** Called after the connection was closed (called once per IO). */
IODINE_CONNECTION_DEF_CB(on_authenticate_sse,
                         IODINE_ON_AUTHENTICATE_SSE_ID,
                         IODINE_CONNECTION_UPGRADE_SSE);
/** Called after the connection was closed (called once per IO). */
IODINE_CONNECTION_DEF_CB(on_authenticate_websocket,
                         IODINE_ON_AUTHENTICATE_WEBSOCKET_ID,
                         IODINE_CONNECTION_UPGRADE_WS);

#undef IODINE_CONNECTION_DEF_CB

typedef struct {
  fio_http_s *h;
  fio_buf_info_s id;
  fio_buf_info_s event;
  fio_buf_info_s data;
} iodine_io_http_on_eventsource_internal_s;

static void *iodine_io_http_on_eventsource_reconnect_internal(void *info_) {
  iodine_io_http_on_eventsource_internal_s *i =
      (iodine_io_http_on_eventsource_internal_s *)info_;
  VALUE connection = (VALUE)fio_http_udata2(i->h);
  if (!connection || connection == Qnil)
    return NULL;
  iodine_connection_s *c = iodine_connection_ptr(connection);
  VALUE args[] = {connection, rb_str_new(i->id.buf, i->id.len)};
  c->store[IODINE_CONNECTION_STORE_tmp] = args[1];
  iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],
                          IODINE_ON_EVENTSOURCE_RECONNECT_ID,
                          2,
                          args);
  c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  return NULL;
}

/** Called when an EventSource reconnect event requests an ID. */
static void iodine_io_http_on_eventsource_reconnect(fio_http_s *h,
                                                    fio_buf_info_s id) {
  /* TODO: FIXME! move into GVL and create message struct to pass to callback */
  VALUE connection = (VALUE)fio_http_udata2(h);
  if (!connection || connection == Qnil)
    return;
  iodine_io_http_on_eventsource_internal_s info = {.h = h, .id = id};
  rb_thread_call_with_gvl(iodine_io_http_on_eventsource_reconnect_internal,
                          &info);
}

static void *iodine_io_http_on_eventsource_internal(void *info_) {
  iodine_io_http_on_eventsource_internal_s *i =
      (iodine_io_http_on_eventsource_internal_s *)info_;
  VALUE connection = (VALUE)fio_http_udata2(i->h);
  if (!connection || connection == Qnil)
    return NULL;
  iodine_connection_s *c = iodine_connection_ptr(connection);
  fio_msg_s msg = {
      .channel = i->event,
      .message = i->data,
  };
  VALUE args[] = {connection, iodine_pubsub_msg_new(&msg)};
  iodine_pubsub_msg_id_set(args[1], rb_str_new(i->id.buf, i->id.len));
  iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],
                          IODINE_ON_EVENTSOURCE_ID,
                          2,
                          args);
  STORE.release(args[1]); /* Store.hold(m) called iodine_pubsub_msg_new */
  return NULL;
}
/** Called when an EventSource event is received. */
static void iodine_io_http_on_eventsource(fio_http_s *h,
                                          fio_buf_info_s id,
                                          fio_buf_info_s event,
                                          fio_buf_info_s data) {
  iodine_io_http_on_eventsource_internal_s info = {.h = h,
                                                   .id = id,
                                                   .event = event,
                                                   .data = data};
  rb_thread_call_with_gvl(iodine_io_http_on_eventsource_internal, &info);
}

typedef struct {
  fio_http_s *h;
  fio_buf_info_s msg;
  uint8_t is_text;
} iodine_io_http_on_message_internal_s;

/** Called when a WebSocket / SSE message arrives. */
static void *iodine_io_http_on_message_internal(void *info) {
  iodine_io_http_on_message_internal_s *i =
      (iodine_io_http_on_message_internal_s *)info;
  VALUE connection = (VALUE)fio_http_udata2(i->h);
  if (!connection || connection == Qnil)
    return NULL;
  iodine_connection_s *c = iodine_connection_ptr(connection);
  VALUE args[] = {connection, rb_str_new(i->msg.buf, i->msg.len)};
  c->store[IODINE_CONNECTION_STORE_tmp] = args[1];
  rb_enc_associate(args[1],
                   i->is_text ? IodineUTF8Encoding : IodineBinaryEncoding);
  iodine_ruby_call_inside(c->store[IODINE_CONNECTION_STORE_handler],
                          IODINE_ON_MESSAGE_ID,
                          2,
                          args);
  c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  return NULL;
}

/** Called when a WebSocket / SSE message arrives. */
static void iodine_io_http_on_message(fio_http_s *h,
                                      fio_buf_info_s msg,
                                      uint8_t is_text) {
  iodine_io_http_on_message_internal_s info = {
      .h = h,
      .msg = msg,
      .is_text = is_text,
  };
  rb_thread_call_with_gvl(iodine_io_http_on_message_internal, &info);
}

/** Called when a request / response cycle is finished with no Upgrade. */
static void iodine_io_http_on_finish(fio_http_s *h) {
  VALUE connection = (VALUE)fio_http_udata2(h);
  if (!connection || connection == Qnil)
    return; /* done in the `on_close` */
  iodine_connection_s *c = iodine_connection_ptr(connection);
  if (c->http) {
    (((c->flags & IODINE_CONNECTION_CLIENT)) ? iodine_ruby_call_outside
                                             : iodine_ruby_call_inside)(
        (iodine_caller_args_s){c->store[IODINE_CONNECTION_STORE_handler],
                               IODINE_ON_FINISH_ID,
                               1,
                               &connection});
    fio_http_free(h);
  }
  c->http = NULL;
  c->io = NULL;
  STORE.release(connection);
}

/* *****************************************************************************
Subscription Helpers
***************************************************************************** */

static void *iodine_connection_on_pubsub_in_gvl(void *m_) {
  fio_msg_s *m = (fio_msg_s *)m_;
  VALUE msg = iodine_pubsub_msg_new(m);
  /* TODO! move callback to async queue? Is this possible? */
  iodine_ruby_call_inside((VALUE)m->udata, IODINE_CALL_ID, 1, &msg);
  STORE.release(msg);
  return m_;
}

static void iodine_connection_on_pubsub(fio_msg_s *m) {
  rb_thread_call_with_gvl(iodine_connection_on_pubsub_in_gvl, m);
}

FIO_IFUNC VALUE iodine_connection_subscribe_internal(fio_io_s *io,
                                                     int argc,
                                                     VALUE *argv) {
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  int64_t filter = 0;
  VALUE proc = Qnil;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(channel, 0, "channel", 0),
                  IODINE_ARG_NUM(filter, 0, "filter", 0),
                  IODINE_ARG_PROC(proc, 0, "callback", 0));

  if ((size_t)filter & (~(size_t)0xFFFF))
    rb_raise(rb_eRangeError,
             "filter out of range (%lld > 0xFFFF)",
             (long long)filter);
  if (!io && proc == Qnil)
    rb_raise(rb_eArgError,
             "Global subscriptions require a callback (proc/block) object!");
  STORE.hold(proc);
  fio_subscribe(.io = io,
                .filter = (int16_t)filter,
                .channel = channel,
                .udata = (void *)proc,
                .queue = fio_io_async_queue(&IODINE_THREAD_POOL),
                .on_message =
                    (!proc || (proc == Qnil) ? NULL
                                             : iodine_connection_on_pubsub),
                .on_unsubscribe = (void (*)(void *))STORE.release);
  return proc;
}

static VALUE iodine_connection_unsubscribe_internal(fio_io_s *io,
                                                    int argc,
                                                    VALUE *argv) {
  int64_t filter = 0;
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(channel, 0, "channel", 0),
                  IODINE_ARG_NUM(filter, 0, "filter", 0));
  if ((size_t)filter & (~(size_t)0xFFFF))
    rb_raise(rb_eRangeError,
             "filter out of range (%lld > 0xFFFF)",
             (long long)filter);
  return fio_unsubscribe(.io = io, .channel = channel, .filter = filter)
             ? RUBY_Qfalse
             : RUBY_Qtrue;
}

/* *****************************************************************************
Listening Argument Parsing
***************************************************************************** */

FIO_IFUNC iodine_connection_args_s iodine_connection_parse_args(int argc,
                                                                VALUE *argv) {
  iodine_connection_args_s r = {
      .url = fio_cli_get_str("-b"),
      .rb_tls = ((fio_cli_get_bool("-tls") ||
                  (fio_cli_get("-cert") && fio_cli_get("-key")))
                     ? Qtrue
                     : Qnil),
      .headers = Qnil,
      .cookies = Qnil,
      .settings =
          {
              /* defaults */
              .on_http = iodine_io_http_on_http,
              .on_stop = iodine_io_http_on_stop,
              .on_finish = iodine_io_http_on_finish,
              .on_authenticate_sse = iodine_io_http_on_authenticate_sse,
              .on_authenticate_websocket =
                  iodine_io_http_on_authenticate_websocket,
              .on_open = iodine_io_http_on_open,
              .on_message = iodine_io_http_on_message,
              .on_eventsource = iodine_io_http_on_eventsource,
              .on_eventsource_reconnect =
                  iodine_io_http_on_eventsource_reconnect,
              .on_ready = iodine_io_http_on_ready,
              .on_shutdown = iodine_io_http_on_shutdown,
              .on_close = iodine_io_http_on_close,
              .queue = &IODINE_THREAD_POOL,
              .public_folder = FIO_STR_INFO1((char *)fio_cli_get("-www")),
              .max_age = (size_t)fio_cli_get_i("-maxage"),
              .max_header_size = (uint32_t)fio_cli_get_i("-maxhd"),
              .max_line_len = (uint32_t)fio_cli_get_i("-maxln"),
              .max_body_size = (size_t)fio_cli_get_i("-maxbd"),
              .ws_max_msg_size = (size_t)fio_cli_get_i("-maxms"),
              .timeout = (uint8_t)fio_cli_get_i("-k"),
              .ws_timeout = (uint8_t)fio_cli_get_i("-ping"),
              .sse_timeout = (uint8_t)fio_cli_get_i("-ping"),
              .log = fio_cli_get_bool("-v"),
          },
  };
  VALUE proc = Qnil, handler_tmp = Qnil;
  iodine_rb2c_arg(
      argc,
      argv,
      IODINE_ARG_BUF(r.url, 0, "url", 0),
      IODINE_ARG_RB(handler_tmp, 0, "handler", 0),
      IODINE_ARG_BUF(r.hint, 0, "service", 0),
      IODINE_ARG_RB(r.rb_tls, 0, "tls", 0),
      IODINE_ARG_STR(r.settings.public_folder, 0, "public", 0),
      IODINE_ARG_SIZE_T(r.settings.max_age, 0, "max_age", 0),
      IODINE_ARG_U32(r.settings.max_header_size, 0, "max_header_size", 0),
      IODINE_ARG_U32(r.settings.max_line_len, 0, "max_line_len", 0),
      IODINE_ARG_SIZE_T(r.settings.max_body_size, 0, "max_body_size", 0),
      IODINE_ARG_SIZE_T(r.settings.ws_max_msg_size, 0, "max_msg_size", 0),
      IODINE_ARG_U8(r.settings.timeout, 0, "timeout", 0),
      IODINE_ARG_U8(r.settings.ws_timeout, 0, "ping", 0),
      IODINE_ARG_U8(r.settings.log, 0, "log", 0),
      IODINE_ARG_BUF(r.method, 0, "method", 0),
      IODINE_ARG_RB(r.headers, 0, "headers", 0),
      IODINE_ARG_BUF(r.body, 0, "body", 0),
      IODINE_ARG_RB(r.cookies, 0, "cookies", 0),
      IODINE_ARG_PROC(proc, 0, "block", 0));
  r.settings.udata = (void *)handler_tmp;
  /* test for errors before allocating or protecting data */

  if (r.headers != Qnil && !RB_TYPE_P(r.headers, RUBY_T_HASH))
    rb_raise(rb_eTypeError, "headers (if provided) MUST be type Hash");
  if (r.cookies != Qnil && !RB_TYPE_P(r.cookies, RUBY_T_HASH))
    rb_raise(rb_eTypeError, "cookies (if provided) MUST be type Hash");

  if (!r.settings.udata || (VALUE)r.settings.udata == Qnil) {
    r.settings.udata = (void *)proc;
    if (proc == Qnil && r.settings.public_folder.buf)
      r.settings.udata = (void *)iodine_rb_IODINE_BASE_APP404;
  }
  if (IODINE_STORE_IS_SKIP(((VALUE)r.settings.udata)))
    rb_raise(
        rb_eArgError,
        "Either a `:handler` or `&block` must be provided and a valid Object!");
  if (r.url.buf)
    r.url_data = fio_url_parse(r.url.buf, r.url.len);
  if (!r.hint.len)
    r.hint = r.url_data.scheme;
  if (r.hint.len > 8)
    rb_raise(rb_eArgError, "service hint too long");
  FIO_LOG_DDEBUG("iodine connection hint: %.*s", (int)r.hint.len, r.hint.buf);
  if (!r.hint.buf || !r.hint.len ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"sses", 3)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"sses", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"SSES", 3)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"SSES", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"unixs", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"UNIXS", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"unixs", 5)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"UNIXS", 5)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"files", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"FILES", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"files", 5)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"FILES", 5)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"https", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"HTTPS", 4)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"https", 5)) ||
      FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"HTTPS", 5)))
    r.service = IODINE_SERVICE_HTTP;
  else if (FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"wss", 2)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"WSS", 2)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"wss", 3)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"WSS", 3)))
    r.service = IODINE_SERVICE_WS;
  else if (FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"tcps", 3)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"TCPS", 3)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"tcps", 4)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"TCPS", 4)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"raws", 3)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"RAWS", 3)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"raws", 4)) ||
           FIO_BUF_INFO_IS_EQ(r.hint, FIO_BUF_INFO2((char *)"RAWS", 4)))
    r.service = IODINE_SERVICE_RAW;
  else {
    rb_raise(rb_eArgError,
             "URL scheme or service type hint error!\n\t"
             "Must be either https, http, ws, wss, sse, sses, tcp or tcps "
             "(case sensitive).");
    r.service = IODINE_SERVICE_UNKNOWN;
  }

  /* No errors, start protecting Ruby data from GC */
  STORE.hold((VALUE)r.settings.udata);
  /* make sure all proper callbacks are defined. */
  iodine_handler_method_injection(Qnil, (VALUE)r.settings.udata);

  r.settings.sse_timeout = r.settings.ws_timeout;
  if (r.rb_tls != Qnil && r.rb_tls != Qfalse) {
    rb_ivar_set((VALUE)r.settings.udata,
                rb_intern2("__iodine__internal__tls", 23),
                r.rb_tls);

    r.settings.tls =
        (r.rb_tls == Qtrue ? fio_io_tls_new()
                           : fio_io_tls_dup(iodine_tls_get(r.rb_tls)));
    if (r.rb_tls == Qtrue && fio_cli_get("-cert") && fio_cli_get("-key"))
      fio_io_tls_cert_add(r.settings.tls,
                          fio_cli_get("-name"),
                          fio_cli_get("-cert"),
                          fio_cli_get("-key"),
                          fio_cli_get("-tls-pass"));
  }

  fio_cli_set("-b", NULL);
  return r;
}

/* *****************************************************************************
Ruby Public API.
***************************************************************************** */

static void iodine_connection___client_headers_add(fio_http_s *h,
                                                   fio_str_info_s n,
                                                   fio_str_info_s v) {
  char *eol = (char *)memchr(v.buf, '\n', v.len);
  while (eol) {
    ++eol;
    fio_str_info_s tmp = FIO_STR_INFO2(v.buf, (size_t)(eol - v.buf));
    fio_http_request_header_add(h, n, tmp);
    v.buf = eol;
    v.len -= tmp.len;
  }
  fio_http_request_header_add(h, n, v);
}

static int iodine_connection___client_headers(VALUE n, VALUE v, VALUE h_) {
  FIO_STR_INFO_TMP_VAR(num, 32);
  fio_http_s *h = (fio_http_s *)h_;
  fio_str_info_s name;
  fio_str_info_s val;
  if (RB_TYPE_P(v, RUBY_T_ARRAY))
    goto is_array;
  if (RB_TYPE_P(n, RUBY_T_SYMBOL))
    n = rb_sym_to_s(n);
  if (!RB_TYPE_P(n, RUBY_T_STRING))
    return ST_CONTINUE;
  name = (fio_str_info_s)IODINE_RSTR_INFO(n);

  if (RB_TYPE_P(v, RUBY_T_SYMBOL))
    v = rb_sym_to_s(v);
  if (RB_TYPE_P(v, RUBY_T_STRING))
    val = (fio_str_info_s)IODINE_RSTR_INFO(v);
  else if (RB_TYPE_P(v, RUBY_T_FIXNUM) &&
           !fio_string_write_i(&num, NULL, FIX2LONG(v)))
    val = num;
  else
    return ST_CONTINUE;

  iodine_connection___client_headers_add(h, name, val);
  return ST_CONTINUE;
is_array:
  for (size_t i = 0; i < (size_t)RARRAY_LEN(v); ++i) {
    VALUE t = RARRAY_PTR(v)[i];
    if (!RB_TYPE_P(t, RUBY_T_STRING))
      continue;
    iodine_connection___client_headers_add(h,
                                           (fio_str_info_s)IODINE_RSTR_INFO(n),
                                           (fio_str_info_s)IODINE_RSTR_INFO(t));
  }
  return ST_CONTINUE;
}

static int iodine_connection___client_cookie(VALUE n, VALUE v, VALUE h_) {
  fio_http_s *h = (fio_http_s *)h_;
  if (!RB_TYPE_P(n, RUBY_T_STRING) || !RB_TYPE_P(v, RUBY_T_STRING))
    goto not_string;
  fio_http_cookie_set(h,
                      .name = IODINE_RSTR_INFO(n),
                      .value = IODINE_RSTR_INFO(v));
  return ST_CONTINUE;

not_string:
  FIO_LOG_WARNING("Client cookie name and value MUST be type String");
  return ST_CONTINUE;
}

/** Called after the connection was closed (called once per IO). */
static void iodine_io_raw_client_on_close(void *buf, void *udata) {
  VALUE connection = (VALUE)udata;
  if (!connection || connection == Qnil)
    return;
  iodine_connection_s *c = iodine_connection_ptr(connection);
  if (c) {
    iodine_ruby_call_outside(c->store[IODINE_CONNECTION_STORE_handler],
                             IODINE_ON_CLOSE_ID,
                             1,
                             &connection);
    fio_io_protocol_s *p = fio_io_protocol(c->io);
    FIO_MEM_FREE(p, sizeof(*p));
  }
  STORE.release(connection);
  (void)buf;
}

/** Initializes a Connection object. */
static VALUE iodine_connection_initialize(int argc, VALUE *argv, VALUE self) {
  // if (!argc)
  //   rb_raise(rb_eException, "Iodine::Connection.new shouldn't be called!");
  STORE.hold(self);
  iodine_connection_s *c = iodine_connection_ptr(self);
  iodine_connection_args_s args = iodine_connection_parse_args(argc, argv);
  if (!args.url.len)
    rb_raise(
        rb_eException,
        "Iodine::Connection.new client requires a valid URL to connect to!\n\t"
        "See documentation, use tcp(s)://.../  or  http(s)://.../  or  "
        "ws(s):/.../ etc'");
  /* test if HTTP scheme */
  if (args.service == IODINE_SERVICE_HTTP ||
      args.service == IODINE_SERVICE_WS) {

    fio_http_s *h = fio_http_new();

    if (args.headers != Qnil)
      rb_hash_foreach(args.headers,
                      iodine_connection___client_headers,
                      (VALUE)h);
    if (args.cookies != Qnil)
      rb_hash_foreach(args.cookies,
                      iodine_connection___client_cookie,
                      (VALUE)h);
    // args.settings.on_finish =
    c->io = fio_http_connect FIO_NOOP(args.url.buf, h, args.settings);
    c->http = h;
    fio_http_udata2_set(h, (void *)self);

  } else { /* Raw Connection */
    rb_raise(rb_eException,
             "Iodine::Connection.new using Raw Sockets is on the TODO list...");
    fio_io_protocol_s *protocol =
        FIO_MEM_REALLOC(NULL, 0, sizeof(*protocol), 0);
    FIO_ASSERT_ALLOC(protocol);
    *protocol = IODINE_RAW_PROTOCOL;
    protocol->on_close = iodine_io_raw_client_on_close;
    protocol->timeout = 1000UL * (uint32_t)args.settings.ws_timeout;
    c->io = fio_io_connect(args.url.buf,
                           .protocol = protocol,
                           .udata = args.settings.udata,
                           .tls = args.settings.tls,
                           .on_failed = iodine_tcp_on_stop);
  }
  c->store[IODINE_CONNECTION_STORE_handler] = (VALUE)args.settings.udata;
  c->flags |= IODINE_CONNECTION_CLIENT;
  return self;
}

/** Returns true if the connection appears to be open (no known issues). */
static VALUE iodine_connection_is_open(VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (c->http)
    return ((fio_http_is_websocket(c->http) && !fio_http_is_finished(c->http))
                ? Qfalse
                : Qtrue);
  if (c->io)
    return (fio_io_is_open(c->io) ? Qtrue : Qfalse);
  return Qfalse;
}

/**
 * Returns the number of bytes that need to be sent before the next
 * `on_drained` callback is called.
 */
static VALUE iodine_connection_pending(VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (c && c->io)
    return RB_SIZE2NUM(((size_t)fio_io_backlog(c->io)));
  return Qfalse;
}

/** Schedules the connection to be closed. */
static VALUE iodine_connection_close(VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (c) {
    if (c->http) {
      /* avoid `rack.input` call to `close` closing the HTTP connection. */
      if (fio_http_is_upgraded(c->http))
        fio_http_close(c->http);
    } else if (c->io)
      fio_io_close(c->io);
    c->flags |= IODINE_CONNECTION_CLOSED;
  }
  return self;
}

/**
 * @deprecated use `Server.extensions[:pubsub]` instead.
 *
 * Always returns true, since Iodine connections support the pub/sub
 * extension.
 */
static VALUE iodine_connection_has_pubsub(VALUE self) {
  FIO_LOG_WARNING(
      "pubsub? is deprecated. Test using `Server.extensions[:pubsub]` or "
      "`#respond_to?(:subscribe)` instead.");
  return Qtrue;
}

/** Writes data to the connection asynchronously. */
static VALUE iodine_connection_write_header___value(fio_http_s *h,
                                                    fio_str_info_s n,
                                                    VALUE v,
                                                    size_t depth) {
  VALUE r = Qtrue;
  FIO_STR_INFO_TMP_VAR(vstr, 24);

  if (depth > 31)
    goto too_deep;
  if (v == Qnil || v == Qfalse)
    goto skip;

  if (RB_TYPE_P(v, RUBY_T_ARRAY))
    goto is_array;
  if (RB_TYPE_P(v, RUBY_T_SYMBOL))
    v = rb_sym_to_s(v);
  if (RB_TYPE_P(v, RUBY_T_STRING))
    vstr = (fio_str_info_s)IODINE_RSTR_INFO(v);
  else if (RB_TYPE_P(v, RUBY_T_FIXNUM))
    fio_string_write_i(&vstr, NULL, (int64_t)RB_NUM2LL(v));
  else
    goto type_error;
  iodine_connection___add_header(h, n, vstr);
  return r;

is_array:
  for (size_t i = 0; i < (size_t)RARRAY_LEN(v); ++i) {
    VALUE t = RARRAY_PTR(v)[i];
    iodine_connection_write_header___value(h, n, t, depth + 1);
  }
  return r;

type_error:
  r = Qfalse;
  rb_raise(rb_eTypeError,
           "write_header called with a non-String header value(s).");
  return r;

too_deep:
  r = Qfalse;
  rb_raise(rb_eTypeError,
           "write_header called with deeply nested Arrays. Nesting should be "
           "avoided.");
  return r;

skip:
  return r;
}

/** Writes data to the connection asynchronously. */
static VALUE iodine_connection_write_header(VALUE self, VALUE name, VALUE v) {
  VALUE r = Qfalse;
  iodine_connection_s *c = iodine_connection_ptr(self);
  fio_http_s *h = c->http;
  if (!fio_http_is_clean(h))
    return r;

  fio_str_info_s header_rb;

  if (RB_TYPE_P(name, RUBY_T_SYMBOL))
    name = rb_sym_to_s(name);
  if (!RB_TYPE_P(name, RUBY_T_STRING))
    goto type_error;

  c->store[IODINE_CONNECTION_STORE_tmp] = name;

  header_rb = (fio_str_info_s)IODINE_RSTR_INFO(name);
  IODINE___COPY_TO_LOWER_CASE(n, header_rb);
  c->store[IODINE_CONNECTION_STORE_tmp] = Qnil;

  if (v == Qnil || v == Qfalse)
    goto clear_header;

  r = iodine_connection_write_header___value(h, n, v, 0);
  return r;

clear_header:
  fio_http_response_header_set(h, n, FIO_STR_INFO0);
  return r;

type_error:
  r = Qfalse;
  rb_raise(rb_eTypeError, "write_header called with non-String header name.");
  return r;
}

/** Writes data to the connection asynchronously. */
FIO_IFUNC VALUE iodine_connection_write_internal(VALUE self,
                                                 VALUE data,
                                                 _Bool finish) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (!c || (!c->http && !c->io))
    return Qfalse;
  fio_str_info_s to_write;
  unsigned to_copy = 1;
  int fileno;
  void (*dealloc)(void *) = NULL;

  /* TODO! SSE connections? id / event / data combo? */

  if (RB_TYPE_P(data, RUBY_T_SYMBOL))
    data = rb_sym_to_s(data);
  if (RB_TYPE_P(data, RUBY_T_STRING)) {
    to_write = FIO_STR_INFO2(RSTRING_PTR(data), (size_t)RSTRING_LEN(data));
    // TODO: use Ruby encoding info for WebSocket?
    // fio_http_websocket_write(c->http, to_write.buf, len, is_text)
  } else if (data == Qnil) {
    to_write = FIO_STR_INFO0;
  } else if (rb_respond_to(data, IODINE_FILENO_ID)) {
    goto is_file;
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
                   .copy = to_copy,
                   .finish = finish);
  else if (c->io) {
    fio_io_write2(c->io,
                  .buf = to_write.buf,
                  .len = to_write.len,
                  .dealloc = dealloc,
                  .copy = to_copy);
    if (finish)
      fio_io_close(c->io);
  }
  return Qtrue;

is_file:
  fileno = fio_file_dup(FIX2INT(rb_funcallv(data, IODINE_FILENO_ID, 0, NULL)));
  if (rb_respond_to(data, IODINE_CLOSE_ID))
    rb_funcallv(data, IODINE_CLOSE_ID, 0, NULL);
  if (fileno == -1)
    return Qfalse;
  if (c->http)
    fio_http_write(c->http, .fd = fileno, .finish = finish);
  else if (c->io) {
    fio_io_write2(c->io, .fd = fileno);
    if (finish)
      fio_io_close(c->io);
  }
  return Qtrue;
}

/** Writes data to the connection asynchronously. */
static VALUE iodine_connection_write(VALUE self, VALUE data) {
  return iodine_connection_write_internal(self, data, 0);
}

/** Writes data to the connection asynchronously. */
static VALUE iodine_connection_write_sse(int argc, VALUE *argv, VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (!c || !c->http)
    return Qfalse;
  fio_buf_info_s to_write;
  fio_buf_info_s id = {0}, event = {0};
  VALUE data = Qnil;
  void (*dealloc)(void *) = NULL;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(id, 0, "id", 0),
                  IODINE_ARG_BUF(event, 0, "event", 0),
                  IODINE_ARG_RB(data, 0, "data", 0));
  if (data == Qnil)
    return Qfalse; /* no data payload */
  if (RB_TYPE_P(data, RUBY_T_SYMBOL))
    data = rb_sym_to_s(data);
  if (RB_TYPE_P(data, RUBY_T_STRING)) {
    to_write = (fio_buf_info_s)IODINE_RSTR_INFO(data);
  } else if (rb_respond_to(data, IODINE_CLOSE_ID)) {
    rb_funcallv(data, IODINE_CLOSE_ID, 0, NULL);
    return Qfalse;
  } else if (fio_http_is_sse(c->http)) {
    dealloc = (void (*)(void *))fio_bstr_free;
    to_write = fio_bstr_buf(iodine_json_stringify2bstr(NULL, data));
  }
  /* don't test `fio_http_is_sse` earlier, as we may need to close files */
  if (to_write.len && fio_http_is_sse(c->http))
    fio_http_sse_write(c->http, .id = id, .event = event, .data = to_write);
  if (dealloc)
    dealloc(to_write.buf);
  return Qtrue;
}

/** Writes data to the connection asynchronously. */
static VALUE iodine_connection_finish(int argc, VALUE *argv, VALUE self) {
  VALUE data = Qnil;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_RB(data, 0, "data", 0));
  if (data != Qnil)
    return iodine_connection_write_internal(self, data, 1);
  iodine_connection_s *c = iodine_connection_ptr(self);
  if ((!c->http && !c->io) || (c->flags & IODINE_CONNECTION_CLIENT))
    return Qfalse;
  if (c->http) {
    if (fio_http_is_finished(c->http))
      return Qfalse;
    fio_http_write(c->http, .finish = 1);
  } else if (c->io) {
    if (!fio_io_is_open(c->io))
      return Qfalse;
    fio_io_close(c->io);
  }
  return Qtrue;
}

/** Returns the peer's network address or `nil`. */
static VALUE iodine_connection_peer_addr(VALUE self) {
  fio_buf_info_s buf = FIO_BUF_INFO0;
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (c->io)
    buf = fio_sock_peer_addr(fio_io_fd(c->io));
  if (!buf.len)
    return Qnil;
  return rb_str_new(buf.buf, buf.len);
}

/** Returns the published peer's address. If unknown, returns {#peer_addr}. */
static VALUE iodine_connection_from(VALUE self) {
  FIO_STR_INFO_TMP_VAR(addr, 128);
  fio_buf_info_s buf = FIO_BUF_INFO0;
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (c->http) {
    fio_http_from(&addr, c->http);
    buf = FIO_STR2BUF_INFO(addr);
  } else if (c->io)
    buf = fio_sock_peer_addr(fio_io_fd(c->io));
  if (!buf.len)
    return Qnil;
  return rb_str_new(buf.buf, buf.len);
}

/* *****************************************************************************
Ruby Public API - Hijacking (Rack)
***************************************************************************** */

/** Method for calling `env['rack.hijack'].call`. */
static VALUE iodine_connection_rack_hijack(VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (!c->http)
    return Qnil;
  int new_fd = fio_sock_dup(fio_io_fd(fio_http_io(c->http)));
  VALUE nio = rb_io_fdopen(new_fd, O_RDWR, NULL);
  if (nio && nio != Qnil) {
    fio_http_close(c->http);
    fio_http_free(c->http);
    c->http = NULL;
    c->io = NULL;
    if (c->store[IODINE_CONNECTION_STORE_env] &&
        RB_TYPE_P(c->store[IODINE_CONNECTION_STORE_env], RUBY_T_HASH)) {
      rb_hash_aset(c->store[IODINE_CONNECTION_STORE_env],
                   STORE.frozen_str(FIO_STR_INFO1((char *)"rack.hijack_io")),
                   nio);
    }
  } else {
    fio_sock_close(new_fd);
  }
  return nio;
}

/* TODO: Partial Hijack */
FIO_SFUNC int iodine_connection_rack_hijack_partial(iodine_connection_s *c,
                                                    VALUE proc) {
  if (!c->http && !c->io) {
    FIO_LOG_ERROR(
        "iodine_connection_rack_hijack_partial called without connections");
    return -1;
  }
  if (!c->io)
    c->io = fio_http_io(c->http);
  if (!rb_respond_to(proc, IODINE_CALL_ID))
    goto error;
  int new_fd = fio_sock_dup(fio_io_fd(c->io));
  if (new_fd == -1)
    goto error;
  VALUE nio = rb_io_fdopen(new_fd, O_RDWR, NULL);
  if (!nio || nio == Qnil)
    goto error_after_open;
  fio_http_write(c->http, .finish = 1);
  if (c->store[IODINE_CONNECTION_STORE_env] &&
      RB_TYPE_P(c->store[IODINE_CONNECTION_STORE_env], RUBY_T_HASH)) {
    rb_hash_aset(c->store[IODINE_CONNECTION_STORE_env],
                 STORE.frozen_str(FIO_STR_INFO1((char *)"rack.hijack_io")),
                 nio);
  }
  FIO_LOG_INFO("calling IO partial hijack `call`");
  rb_funcallv(proc, IODINE_CALL_ID, 1, &nio);
  return 0;
error_after_open:
  fio_sock_close(new_fd);
error:
  FIO_LOG_ERROR("partial hijack error – does the object answer to `call`?");
  return -1;
}

/* *****************************************************************************
Ruby Public API - Pub/Sub
***************************************************************************** */

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
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (!c || (!c->http && !c->io))
    return Qnil;
  return iodine_connection_unsubscribe_internal(c->io, argc, argv);
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
  return iodine_connection_unsubscribe_internal(NULL, argc, argv);
}

/**
 * Subscribes to a combination of a channel (String) and filter (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the subscription's channel name (String).
 * - `filter`:  the subscription's filter (Number < 32,767).
 * - `callback`: an optional object that answers to `call` and accepts a
 * single argument (the message structure).
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
 *     # with / without a callback
 *     subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil; }
 *     subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
 *
 */
static VALUE iodine_connection_subscribe(int argc, VALUE *argv, VALUE self) {
  iodine_connection_s *c = iodine_connection_ptr(self);
  if (!c || (!c->http && !c->io))
    return Qnil;
  if (c->io)
    iodine_connection_subscribe_internal(c->io, argc, argv);
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
 *     Iodine.subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel ==
 * Qnil;} Iodine.subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
 *
 */
static VALUE iodine_connection_subscribe_klass(int argc,
                                               VALUE *argv,
                                               VALUE self) {
  iodine_connection_subscribe_internal(NULL, argc, argv);
  return self;
}

static VALUE iodine_connection_publish_internal(fio_io_s *io,
                                                int argc,
                                                VALUE *argv,
                                                VALUE self) {
  VALUE message = Qnil;
  VALUE engine = Qnil;
  int64_t filter = 0;
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  fio_buf_info_s msg = FIO_BUF_INFO2(NULL, 0);
  char *to_free = NULL;
  fio_pubsub_engine_s *eng = NULL;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(channel, 0, "channel", 0),
                  IODINE_ARG_RB(message, 0, "message", 0),
                  IODINE_ARG_NUM(filter, 0, "filter", 0),
                  IODINE_ARG_RB(engine, 0, "engine", 0));
  if ((size_t)filter & (~(size_t)0xFFFF))
    rb_raise(rb_eRangeError,
             "filter out of range (%lld > 0xFFFF)",
             (long long)filter);
  if (RB_TYPE_P(message, RUBY_T_SYMBOL))
    message = rb_sym_to_s(message);
  if (RB_TYPE_P(message, RUBY_T_STRING)) {
    msg = FIO_BUF_INFO2(RSTRING_PTR(message), (size_t)RSTRING_LEN(message));
  } else {
    to_free = iodine_json_stringify2bstr(NULL, message);
    msg = fio_bstr_buf(to_free);
  }
  if (engine != Qnil)
    eng = iodine_pubsub_eng_get(engine)->ptr;
  fio_publish(.from = io,
              .filter = (int16_t)filter,
              .channel = channel,
              .message = msg,
              .engine = eng);
  fio_bstr_free(to_free);
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
 *     Iodine.publish(channel: "name", message: "payload")
 */
static VALUE iodine_connection_publish(int argc, VALUE *argv, VALUE self) {
  return iodine_connection_publish_internal(iodine_connection_ptr(self)->io,
                                            argc,
                                            argv,
                                            self);
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
 *     Iodine.publish(channel: "name", message: "payload")
 */
static VALUE iodine_connection_publish_klass(int argc,
                                             VALUE *argv,
                                             VALUE self) {
  return iodine_connection_publish_internal(NULL, argc, argv, self);
}

/* *****************************************************************************
Listen to incoming TCP/IP Connections
***************************************************************************** */

static void iodine_tcp_on_stop(fio_io_protocol_s *p, void *udata) {
  /* TODO! call on_close */
  // VALUE connection = rb_obj_alloc(iodine_rb_IODINE_CONNECTION);
  // STORE.hold(connection);
  // iodine_connection_s *c = iodine_connection_ptr(m);
  // c->store[IODINE_CONNECTION_STORE_handler] = (VALUE)udata;
  // c->io = NULL;
  // c->http = NULL;
  // iodine_ruby_call_outside((VALUE)udata, IODINE_CLOSE_ID, 1, &connection);
  // STORE.release(connection);

  STORE.release((VALUE)udata);
  FIO_MEM_FREE(p, sizeof(*p));
}

static void *iodine_tcp_listen(iodine_connection_args_s args) {
  fio_io_protocol_s *protocol = FIO_MEM_REALLOC(NULL, 0, sizeof(*protocol), 0);
  if (!protocol)
    return NULL;
  STORE.hold((VALUE)args.settings.udata);
  *protocol = IODINE_RAW_PROTOCOL;
  protocol->timeout = 1000UL * (uint32_t)args.settings.ws_timeout;
  FIO_LOG_DEBUG("iodine will listen for raw connections on %.*s",
                (int)args.url.len,
                args.url.buf);
  return fio_io_listen(.url = args.url.buf,
                       .protocol = protocol,
                       .udata = args.settings.udata,
                       .tls = args.settings.tls,
                       .on_stop = iodine_tcp_on_stop,
                       .queue_for_accept = 0);
}

/* *****************************************************************************
Listen to incoming HTTP Connections
***************************************************************************** */

static void iodine_io_http_on_stop(struct fio_http_settings_s *s) {
  STORE.release((VALUE)s->udata);
}

/* *****************************************************************************
Listen function routing
***************************************************************************** */

static VALUE iodine_listen_rb(int argc, VALUE *argv, VALUE self) {
  VALUE r = Qnil;
  iodine_connection_args_s s = iodine_connection_parse_args(argc, argv);
  void *listener = NULL;
  switch (s.service) {
  case IODINE_SERVICE_RAW: listener = iodine_tcp_listen(s); break;
  case IODINE_SERVICE_HTTP:    /* fall through */
  case IODINE_SERVICE_UNKNOWN: /* fall through */
  case IODINE_SERVICE_WS:
    s.settings.on_stop = NULL; /* leak the memory, better than accidental.. */
    listener = fio_http_listen FIO_NOOP(s.url.buf, s.settings);
  }
  fio_io_tls_free(s.settings.tls);
  if (!listener)
    rb_raise(rb_eRuntimeError, "Couldn't open listening socket.");
  if (fio_io_listener_is_tls(listener))
    iodine_env_set_key_pair(IODINE_CONNECTION_ENV_TEMPLATE,
                            FIO_STR_INFO2((char *)"rack.url_scheme", 15),
                            FIO_STR_INFO2((char *)"https", 5));
  r = iodine_listener_new(listener,
                          (VALUE)s.settings.udata,
                          s.service != IODINE_SERVICE_RAW);
  return r;
  (void)self;
}

/* *****************************************************************************
Initialize Connection related STORE Cache
***************************************************************************** */

static void iodine_connection_cache_common_strings(void) {
  fio_str_info_s common_headers[] = {
      FIO_STR_INFO1((char *)""),
      FIO_STR_INFO1((char *)"a-im"),
      FIO_STR_INFO1((char *)"accept"),
      FIO_STR_INFO1((char *)"accept-ch"),
      FIO_STR_INFO1((char *)"accept-charset"),
      FIO_STR_INFO1((char *)"accept-datetime"),
      FIO_STR_INFO1((char *)"accept-encoding"),
      FIO_STR_INFO1((char *)"accept-language"),
      FIO_STR_INFO1((char *)"accept-patch"),
      FIO_STR_INFO1((char *)"accept-ranges"),
      FIO_STR_INFO1((char *)"access-control-allow-credentials"),
      FIO_STR_INFO1((char *)"access-control-allow-headers"),
      FIO_STR_INFO1((char *)"access-control-allow-methods"),
      FIO_STR_INFO1((char *)"access-control-allow-origin"),
      FIO_STR_INFO1((char *)"access-control-expose-headers"),
      FIO_STR_INFO1((char *)"access-control-max-age"),
      FIO_STR_INFO1((char *)"access-control-request-headers"),
      FIO_STR_INFO1((char *)"access-control-request-method"),
      FIO_STR_INFO1((char *)"age"),
      FIO_STR_INFO1((char *)"allow"),
      FIO_STR_INFO1((char *)"alt-svc"),
      FIO_STR_INFO1((char *)"authorization"),
      FIO_STR_INFO1((char *)"cache-control"),
      FIO_STR_INFO1((char *)"common"),
      FIO_STR_INFO1((char *)"connection"),
      FIO_STR_INFO1((char *)"content-disposition"),
      FIO_STR_INFO1((char *)"content-encoding"),
      FIO_STR_INFO1((char *)"content-language"),
      FIO_STR_INFO1((char *)"content-length"),
      FIO_STR_INFO1((char *)"content-location"),
      FIO_STR_INFO1((char *)"content-md5"),
      FIO_STR_INFO1((char *)"content-range"),
      FIO_STR_INFO1((char *)"content-security-policy"),
      FIO_STR_INFO1((char *)"content-type"),
      FIO_STR_INFO1((char *)"cookie"),
      FIO_STR_INFO1((char *)"correlates"),
      FIO_STR_INFO1((char *)"date"),
      FIO_STR_INFO1((char *)"delta-base"),
      FIO_STR_INFO1((char *)"dnt"),
      FIO_STR_INFO1((char *)"etag"),
      FIO_STR_INFO1((char *)"example"),
      FIO_STR_INFO1((char *)"expect"),
      FIO_STR_INFO1((char *)"expect-ct"),
      FIO_STR_INFO1((char *)"expires"),
      FIO_STR_INFO1((char *)"field"),
      FIO_STR_INFO1((char *)"forwarded"),
      FIO_STR_INFO1((char *)"from"),
      FIO_STR_INFO1((char *)"front-end-https"),
      FIO_STR_INFO1((char *)"host"),
      FIO_STR_INFO1((char *)"http2-settings"),
      FIO_STR_INFO1((char *)"if-match"),
      FIO_STR_INFO1((char *)"if-modified-since"),
      FIO_STR_INFO1((char *)"if-none-match"),
      FIO_STR_INFO1((char *)"if-range"),
      FIO_STR_INFO1((char *)"if-unmodified-since"),
      FIO_STR_INFO1((char *)"im"),
      FIO_STR_INFO1((char *)"last-modified"),
      FIO_STR_INFO1((char *)"link"),
      FIO_STR_INFO1((char *)"location"),
      FIO_STR_INFO1((char *)"mandatory"),
      FIO_STR_INFO1((char *)"max-forwards"),
      FIO_STR_INFO1((char *)"must"),
      FIO_STR_INFO1((char *)"nel"),
      FIO_STR_INFO1((char *)"only"),
      FIO_STR_INFO1((char *)"origin"),
      FIO_STR_INFO1((char *)"p3p"),
      FIO_STR_INFO1((char *)"permanent"),
      FIO_STR_INFO1((char *)"permissions-policy"),
      FIO_STR_INFO1((char *)"pragma"),
      FIO_STR_INFO1((char *)"prefer"),
      FIO_STR_INFO1((char *)"preference-applied"),
      FIO_STR_INFO1((char *)"proxy-authenticate"),
      FIO_STR_INFO1((char *)"proxy-authorization"),
      FIO_STR_INFO1((char *)"proxy-connection"),
      FIO_STR_INFO1((char *)"public-key-pins"),
      FIO_STR_INFO1((char *)"range"),
      FIO_STR_INFO1((char *)"referer"),
      FIO_STR_INFO1((char *)"refresh"),
      FIO_STR_INFO1((char *)"report-to"),
      FIO_STR_INFO1((char *)"response"),
      FIO_STR_INFO1((char *)"retry-after"),
      FIO_STR_INFO1((char *)"rfc"),
      FIO_STR_INFO1((char *)"save-data"),
      FIO_STR_INFO1((char *)"sec-ch-ua"),
      FIO_STR_INFO1((char *)"sec-ch-ua-mobile"),
      FIO_STR_INFO1((char *)"sec-ch-ua-platform"),
      FIO_STR_INFO1((char *)"sec-fetch-dest"),
      FIO_STR_INFO1((char *)"sec-fetch-mode"),
      FIO_STR_INFO1((char *)"sec-fetch-site"),
      FIO_STR_INFO1((char *)"sec-fetch-user"),
      FIO_STR_INFO1((char *)"sec-gpc"),
      FIO_STR_INFO1((char *)"server"),
      FIO_STR_INFO1((char *)"set-cookie"),
      FIO_STR_INFO1((char *)"standard"),
      FIO_STR_INFO1((char *)"status"),
      FIO_STR_INFO1((char *)"strict-transport-security"),
      FIO_STR_INFO1((char *)"te"),
      FIO_STR_INFO1((char *)"timing-allow-origin"),
      FIO_STR_INFO1((char *)"tk"),
      FIO_STR_INFO1((char *)"trailer"),
      FIO_STR_INFO1((char *)"transfer-encoding"),
      FIO_STR_INFO1((char *)"upgrade"),
      FIO_STR_INFO1((char *)"upgrade-insecure-requests"),
      FIO_STR_INFO1((char *)"user-agent"),
      FIO_STR_INFO1((char *)"vary"),
      FIO_STR_INFO1((char *)"via"),
      FIO_STR_INFO1((char *)"warning"),
      FIO_STR_INFO1((char *)"when"),
      FIO_STR_INFO1((char *)"www-authenticate"),
      FIO_STR_INFO1((char *)"x-att-deviceid"),
      FIO_STR_INFO1((char *)"x-content-duration"),
      FIO_STR_INFO1((char *)"x-content-security-policy"),
      FIO_STR_INFO1((char *)"x-content-type-options"),
      FIO_STR_INFO1((char *)"x-correlation-id"),
      FIO_STR_INFO1((char *)"x-csrf-token"),
      FIO_STR_INFO1((char *)"x-forwarded-for"),
      FIO_STR_INFO1((char *)"x-forwarded-host"),
      FIO_STR_INFO1((char *)"x-forwarded-proto"),
      FIO_STR_INFO1((char *)"x-frame-options"),
      FIO_STR_INFO1((char *)"x-http-method-override"),
      FIO_STR_INFO1((char *)"x-powered-by"),
      FIO_STR_INFO1((char *)"x-redirect-by"),
      FIO_STR_INFO1((char *)"x-request-id"),
      FIO_STR_INFO1((char *)"x-requested-with"),
      FIO_STR_INFO1((char *)"x-ua-compatible"),
      FIO_STR_INFO1((char *)"x-uidh"),
      FIO_STR_INFO1((char *)"x-wap-profile"),
      FIO_STR_INFO1((char *)"x-webkit-csp"),
      FIO_STR_INFO1((char *)"x-xss-protection"),
      {0},
  };
  for (size_t i = 0; common_headers[i].buf; ++i) {
    STORE.release(STORE.frozen_str(common_headers[i]));
    STORE.release(STORE.header_name(common_headers[i]));
  }
}
/* *****************************************************************************
Initialize Connection Class
***************************************************************************** */

/* Initialize Iodine::Connection */ // clang-format off
/**
 * Iodine::Connection is the connection instance class used for all
 * connection callbacks to control the state of the connection.
 */
static void Init_Iodine_Connection(void)  { 
  rb_define_singleton_method(iodine_rb_IODINE_BASE,
                             "add_missing_handlar_methods",
                             iodine_handler_method_injection, 1);
  VALUE m = iodine_rb_IODINE_CONNECTION = rb_define_class_under(iodine_rb_IODINE, "Connection", rb_cObject);
  rb_define_alloc_func(m, iodine_connection_alloc);
  STORE.hold(iodine_rb_IODINE_CONNECTION);
  IODINE_CONST_ID_STORE(IODINE_SAME_SITE_DEFAULT, "default");
  IODINE_CONST_ID_STORE(IODINE_SAME_SITE_NONE, "none");
  IODINE_CONST_ID_STORE(IODINE_SAME_SITE_LAX, "lax");
  IODINE_CONST_ID_STORE(IODINE_SAME_SITE_STRICT, "strict");

  rb_define_method(m, "initialize", iodine_connection_initialize, -1);

  #define IODINE_DEFINE_GETSET_METHOD(name)\
  rb_define_method(m, #name, iodine_connection_##name##_get, 0);\
  rb_define_method(m, #name "=", iodine_connection_##name##_set, 1)\

  IODINE_DEFINE_GETSET_METHOD(env);
  IODINE_DEFINE_GETSET_METHOD(handler);
  IODINE_DEFINE_GETSET_METHOD(status);
  IODINE_DEFINE_GETSET_METHOD(method);
  IODINE_DEFINE_GETSET_METHOD(path);
  IODINE_DEFINE_GETSET_METHOD(query);
  IODINE_DEFINE_GETSET_METHOD(version);
  #undef IODINE_DEFINE_GETSET_METHOD



  rb_define_method(m, "headers_sent?", iodine_connection_headers_sent_p, 0);
  rb_define_method(m, "valid?", iodine_connection_is_clean, 0);
  rb_define_method(m, "dup", iodine_connection_dup_failer, 0);
  rb_define_method(m, "to_s", iodine_connection_to_s, 0);

  rb_define_method(m, "[]", iodine_connection_map_get, 1);
  rb_define_method(m, "[]=", iodine_connection_map_set, 2);

  rb_define_method(m, "each", iodine_connection_map_each, 0);
  rb_define_method(m, "store_count", iodine_connection_map_count, 0);
  rb_define_method(m, "store_capa", iodine_connection_map_capa, 0);
  rb_define_method(m, "store_reserve", iodine_connection_map_reserve, 1);

  rb_define_method(m, "headers", iodine_connection_map_headers, 0);
  rb_define_method(m, "rack_hijack", iodine_connection_rack_hijack, 0);

  rb_define_method(m, "cookie", iodine_connection_cookie_get, 1);
  rb_define_method(m, "set_cookie", iodine_connection_cookie_set, -1);
  rb_define_method(m, "each_cookie", iodine_connection_cookie_each, 0);

  rb_define_method(m, "length", iodine_connection_body_length, 0);
  rb_define_method(m, "gets", iodine_connection_body_gets, -1);
  rb_define_method(m, "read", iodine_connection_body_read, -1);
  rb_define_method(m, "seek", iodine_connection_body_seek, -1);
  rb_define_method(m, "rewind", iodine_connection_body_seek, -1);

  rb_define_method(m, "open?", iodine_connection_is_open, 0);
  rb_define_method(m, "pending", iodine_connection_pending, 0);
  rb_define_method(m, "close", iodine_connection_close, 0);
  rb_define_method(m, "write_header", iodine_connection_write_header, 2);
  rb_define_method(m, "write", iodine_connection_write, 1);
  rb_define_method(m, "write_sse", iodine_connection_write_sse, -1);
  rb_define_method(m, "finish", iodine_connection_finish, -1);


  rb_define_method(m, "websocket?", iodine_connection_is_websocket, 0);
  rb_define_method(m, "sse?", iodine_connection_is_sse, 0);

  rb_define_method(m, "peer_addr", iodine_connection_peer_addr, 0);
  rb_define_method(m, "from", iodine_connection_from, 0);

  rb_define_method(m, "subscribe", iodine_connection_subscribe, -1);
  rb_define_singleton_method(m, "subscribe", iodine_connection_subscribe_klass, -1);
  rb_define_singleton_method(iodine_rb_IODINE, "subscribe", iodine_connection_subscribe_klass, -1);

  rb_define_method(m, "unsubscribe", iodine_connection_unsubscribe, -1);
  rb_define_singleton_method(m, "unsubscribe", iodine_connection_unsubscribe_klass, -1);
  rb_define_singleton_method(iodine_rb_IODINE, "unsubscribe", iodine_connection_unsubscribe_klass, -1);

  rb_define_method(m, "publish", iodine_connection_publish, -1);
  rb_define_singleton_method(m, "publish", iodine_connection_publish_klass, -1);
  rb_define_singleton_method(iodine_rb_IODINE, "publish", iodine_connection_publish_klass, -1);

  rb_define_method(m, "pubsub?", iodine_connection_has_pubsub, 0);
  rb_define_singleton_method(m, "pubsub?", iodine_connection_has_pubsub, 0);
  rb_define_singleton_method(iodine_rb_IODINE, "pubsub?", iodine_connection_has_pubsub, 0);

  iodine_connection_cache_common_strings();
  iodine_connection_init_env_template(FIO_BUF_INFO0);
} // clang-format on

#endif /* H___IODINE_CONNECTION___H */
