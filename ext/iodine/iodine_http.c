/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine.h"

#include "http.h"

#include <ruby/encoding.h>
#include <ruby/io.h>
// #include "iodine_websockets.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* *****************************************************************************
Available Globals
***************************************************************************** */

typedef struct {
  VALUE app;
  VALUE env;
} iodine_http_settings_s;

/* these three are used also by iodin_rack_io.c */
VALUE IODINE_R_INPUT_DEFAULT;
VALUE IODINE_R_INPUT;
VALUE IODINE_R_HIJACK;
VALUE IODINE_R_HIJACK_IO;
VALUE IODINE_R_HIJACK_CB;

static VALUE RACK_UPGRADE;
static VALUE RACK_UPGRADE_Q;
static VALUE RACK_UPGRADE_SSE;
static VALUE RACK_UPGRADE_WEBSOCKET;
static VALUE UPGRADE_TCP;

static VALUE HTTP_ACCEPT;
static VALUE HTTP_USER_AGENT;
static VALUE HTTP_ACCEPT_ENCODING;
static VALUE HTTP_ACCEPT_LANGUAGE;
static VALUE HTTP_CONNECTION;
static VALUE HTTP_HOST;

static VALUE hijack_func_sym;
static ID close_method_id;
static ID each_method_id;
static ID attach_method_id;
static ID iodine_call_proc_id;

static VALUE env_template_no_upgrade;
static VALUE env_template_websockets;
static VALUE env_template_sse;

static rb_encoding *IodineUTF8Encoding;
static rb_encoding *IodineBinaryEncoding;

static uint8_t support_xsendfile = 0;

#define rack_declare(rack_name) static VALUE rack_name

#define rack_set(rack_name, str)                                               \
  (rack_name) = rb_enc_str_new((str), strlen((str)), IodineBinaryEncoding);    \
  rb_global_variable(&(rack_name));                                            \
  rb_obj_freeze(rack_name);
#define rack_set_sym(rack_name, sym)                                           \
  (rack_name) = rb_id2sym(rb_intern2((sym), strlen((sym))));                   \
  rb_global_variable(&(rack_name));

#define rack_autoset(rack_name) rack_set((rack_name), #rack_name)

// static uint8_t IODINE_IS_DEVELOPMENT_MODE = 0;

rack_declare(HTTP_SCHEME);
rack_declare(HTTPS_SCHEME);
rack_declare(QUERY_ESTRING);
rack_declare(REQUEST_METHOD);
rack_declare(PATH_INFO);
rack_declare(QUERY_STRING);
rack_declare(QUERY_ESTRING);
rack_declare(SERVER_NAME);
rack_declare(SERVER_PORT);
rack_declare(SERVER_PROTOCOL);
rack_declare(HTTP_VERSION);
rack_declare(REMOTE_ADDR);
rack_declare(CONTENT_LENGTH);
rack_declare(CONTENT_TYPE);
rack_declare(R_URL_SCHEME);          // rack.url_scheme
rack_declare(R_INPUT);               // rack.input
rack_declare(XSENDFILE);             // for X-Sendfile support
rack_declare(XSENDFILE_TYPE);        // for X-Sendfile support
rack_declare(XSENDFILE_TYPE_HEADER); // for X-Sendfile support
rack_declare(CONTENT_LENGTH_HEADER); // for X-Sendfile support

/* used internally to handle requests */
typedef struct {
  http_s *h;
  FIOBJ body;
  enum iodine_http_response_type_enum {
    IODINE_HTTP_NONE,
    IODINE_HTTP_SENDBODY,
    IODINE_HTTP_XSENDFILE,
    IODINE_HTTP_EMPTY,
    IODINE_HTTP_ERROR,
  } type;
  enum iodine_upgrade_type_enum {
    IODINE_UPGRADE_NONE = 0,
    IODINE_UPGRADE_WEBSOCKET,
    IODINE_UPGRADE_SSE,
  } upgrade;
} iodine_http_request_handle_s;

/* *****************************************************************************
WebSocket support
***************************************************************************** */

typedef struct {
  char *data;
  size_t size;
  uint8_t is_text;
  VALUE io;
} iodine_msg2ruby_s;

static void *iodine_ws_fire_message(void *msg_) {
  iodine_msg2ruby_s *msg = msg_;
  VALUE data = rb_enc_str_new(
      msg->data, msg->size,
      (msg->is_text ? rb_utf8_encoding() : rb_ascii8bit_encoding()));
  iodine_connection_fire_event(msg->io, IODINE_CONNECTION_ON_MESSAGE, data);
  return NULL;
}

static void iodine_ws_on_message(ws_s *ws, fio_str_info_s data,
                                 uint8_t is_text) {
  iodine_msg2ruby_s msg = {
      .data = data.data,
      .size = data.len,
      .is_text = is_text,
      .io = (VALUE)websocket_udata_get(ws),
  };
  IodineCaller.enterGVL(iodine_ws_fire_message, &msg);
}
/**
 * The (optional) on_open callback will be called once the websocket
 * connection is established and before is is registered with `facil`, so no
 * `on_message` events are raised before `on_open` returns.
 */
static void iodine_ws_on_open(ws_s *ws) {
  VALUE h = (VALUE)websocket_udata_get(ws);
  iodine_connection_s *c = iodine_connection_CData(h);
  c->arg = ws;
  c->uuid = websocket_uuid(ws);
  iodine_connection_fire_event(h, IODINE_CONNECTION_ON_OPEN, Qnil);
}
/**
 * The (optional) on_ready callback will be after a the underlying socket's
 * buffer changes it's state from full to empty.
 *
 * If the socket's buffer is never used, the callback is never called.
 */
static void iodine_ws_on_ready(ws_s *ws) {
  iodine_connection_fire_event((VALUE)websocket_udata_get(ws),
                               IODINE_CONNECTION_ON_DRAINED, Qnil);
}
/**
 * The (optional) on_shutdown callback will be called if a websocket
 * connection is still open while the server is shutting down (called before
 * `on_close`).
 */
static void iodine_ws_on_shutdown(ws_s *ws) {
  iodine_connection_fire_event((VALUE)websocket_udata_get(ws),
                               IODINE_CONNECTION_ON_SHUTDOWN, Qnil);
}
/**
 * The (optional) on_close callback will be called once a websocket connection
 * is terminated or failed to be established.
 *
 * The `uuid` is the connection's unique ID that can identify the Websocket. A
 * value of `uuid == 0` indicates the Websocket connection wasn't established
 * (an error occured).
 *
 * The `udata` is the user data as set during the upgrade or using the
 * `websocket_udata_set` function.
 */
static void iodine_ws_on_close(intptr_t uuid, void *udata) {
  iodine_connection_fire_event((VALUE)udata, IODINE_CONNECTION_ON_CLOSE, Qnil);
  (void)uuid;
}

static void iodine_ws_attach(http_s *h, VALUE handler, VALUE env) {
  VALUE io =
      iodine_connection_new(.type = IODINE_CONNECTION_WEBSOCKET, .arg = NULL,
                            .handler = handler, .env = env, .uuid = 0);
  if (io == Qnil)
    return;

  http_upgrade2ws(h, .on_message = iodine_ws_on_message,
                  .on_open = iodine_ws_on_open, .on_ready = iodine_ws_on_ready,
                  .on_shutdown = iodine_ws_on_shutdown,
                  .on_close = iodine_ws_on_close, .udata = (void *)io);
}

/* *****************************************************************************
SSE support
***************************************************************************** */

static void iodine_sse_on_ready(http_sse_s *sse) {
  iodine_connection_fire_event((VALUE)sse->udata, IODINE_CONNECTION_ON_DRAINED,
                               Qnil);
}

static void iodine_sse_on_shutdown(http_sse_s *sse) {
  iodine_connection_fire_event((VALUE)sse->udata, IODINE_CONNECTION_ON_SHUTDOWN,
                               Qnil);
}
static void iodine_sse_on_close(http_sse_s *sse) {
  iodine_connection_fire_event((VALUE)sse->udata, IODINE_CONNECTION_ON_CLOSE,
                               Qnil);
}

static void iodine_sse_on_open(http_sse_s *sse) {
  VALUE h = (VALUE)sse->udata;
  iodine_connection_s *c = iodine_connection_CData(h);
  c->arg = sse;
  c->uuid = http_sse2uuid(sse);
  iodine_connection_fire_event(h, IODINE_CONNECTION_ON_OPEN, Qnil);
  sse->on_ready = iodine_sse_on_ready;
  fio_force_event(c->uuid, FIO_EVENT_ON_READY);
}

static void iodine_sse_attach(http_s *h, VALUE handler, VALUE env) {
  VALUE io = iodine_connection_new(.type = IODINE_CONNECTION_SSE, .arg = NULL,
                                   .handler = handler, .env = env, .uuid = 0);
  if (io == Qnil)
    return;

  http_upgrade2sse(h, .on_open = iodine_sse_on_open,
                   .on_ready = NULL /* will be set after the on_open */,
                   .on_shutdown = iodine_sse_on_shutdown,
                   .on_close = iodine_sse_on_close, .udata = (void *)io);
}

/* *****************************************************************************
Copying data from the C request to the Rack's ENV
***************************************************************************** */

#define to_upper(c) (((c) >= 'a' && (c) <= 'z') ? ((c) & ~32) : (c))

static int iodine_copy2env_task(FIOBJ o, void *env_) {
  VALUE env = (VALUE)env_;
  FIOBJ name = fiobj_hash_key_in_loop();
  fio_str_info_s tmp = fiobj_obj2cstr(name);
  VALUE hname = (VALUE)0;
  /* test for common header names, using pre-allocated memory */
  if (tmp.len == 6 && !memcmp("accept", tmp.data, 6)) {
    hname = HTTP_ACCEPT;
  } else if (tmp.len == 10 && !memcmp("user-agent", tmp.data, 10)) {
    hname = HTTP_USER_AGENT;
  } else if (tmp.len == 15 && !memcmp("accept-encoding", tmp.data, 15)) {
    hname = HTTP_ACCEPT_ENCODING;
  } else if (tmp.len == 15 && !memcmp("accept-language", tmp.data, 15)) {
    hname = HTTP_ACCEPT_LANGUAGE;
  } else if (tmp.len == 10 && !memcmp("connection", tmp.data, 10)) {
    hname = HTTP_CONNECTION;
  } else if (tmp.len == 4 && !memcmp("host", tmp.data, 4)) {
    hname = HTTP_HOST;
  } else if (tmp.len > 123) {
    char *buf = fio_malloc(tmp.len + 5);
    memcpy(buf, "HTTP_", 5);
    for (size_t i = 0; i < tmp.len; ++i) {
      buf[i + 5] = (tmp.data[i] == '-') ? '_' : to_upper(tmp.data[i]);
    }
    hname = rb_enc_str_new(buf, tmp.len + 5, IodineBinaryEncoding);
    fio_free(buf);
  } else {
    char buf[128];
    memcpy(buf, "HTTP_", 5);
    for (size_t i = 0; i < tmp.len; ++i) {
      buf[i + 5] = (tmp.data[i] == '-') ? '_' : to_upper(tmp.data[i]);
    }
    hname = rb_enc_str_new(buf, tmp.len + 5, IodineBinaryEncoding);
  }

  if (FIOBJ_TYPE_IS(o, FIOBJ_T_STRING)) {
    tmp = fiobj_obj2cstr(o);
    rb_hash_aset(env, hname,
                 rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));

  } else {
    /* it's an array */
    size_t count = fiobj_ary_count(o);
    VALUE ary = rb_ary_new2(count);
    rb_hash_aset(env, hname, ary);
    for (size_t i = 0; i < count; ++i) {
      tmp = fiobj_obj2cstr(fiobj_ary_index(o, i));
      rb_ary_push(ary, rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
    }
  }
  return 0;
}

static inline VALUE copy2env(iodine_http_request_handle_s *handle) {
  VALUE env;
  http_s *h = handle->h;
  switch (handle->upgrade) {
  case IODINE_UPGRADE_WEBSOCKET:
    env = rb_hash_dup(env_template_websockets);
    break;
  case IODINE_UPGRADE_SSE:
    env = rb_hash_dup(env_template_sse);
    break;
  case IODINE_UPGRADE_NONE: /* fallthrough */
  default:
    env = rb_hash_dup(env_template_no_upgrade);
    break;
  }
  IodineStore.add(env);

  fio_str_info_s tmp;
  char *pos = NULL;
  /* Copy basic data */
  tmp = fiobj_obj2cstr(h->method);
  rb_hash_aset(env, REQUEST_METHOD,
               rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
  tmp = fiobj_obj2cstr(h->path);
  rb_hash_aset(env, PATH_INFO,
               rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
  if (h->query) {
    tmp = fiobj_obj2cstr(h->query);
    rb_hash_aset(env, QUERY_STRING,
                 tmp.len
                     ? rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding)
                     : QUERY_ESTRING);
  } else {
    rb_hash_aset(env, QUERY_STRING, QUERY_ESTRING);
  }
  {
    // HTTP version appears twice
    tmp = fiobj_obj2cstr(h->version);
    VALUE hname = rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding);
    rb_hash_aset(env, SERVER_PROTOCOL, hname);
    rb_hash_aset(env, HTTP_VERSION, hname);
  }

  { // Support for Ruby web-console.
    fio_str_info_s peer = http_peer_addr(h);
    if (peer.len) {
      rb_hash_aset(env, REMOTE_ADDR, rb_str_new(peer.data, peer.len));
    }
  }

  /* handle the HOST header, including the possible host:#### format*/
  static uint64_t host_hash = 0;
  if (!host_hash)
    host_hash = fiobj_hash_string("host", 4);
  tmp = fiobj_obj2cstr(fiobj_hash_get2(h->headers, host_hash));
  pos = tmp.data;
  while (*pos && *pos != ':')
    pos++;
  if (*pos == 0) {
    rb_hash_aset(env, SERVER_NAME,
                 rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
    rb_hash_aset(env, SERVER_PORT, QUERY_ESTRING);
  } else {
    rb_hash_aset(
        env, SERVER_NAME,
        rb_enc_str_new(tmp.data, pos - tmp.data, IodineBinaryEncoding));
    ++pos;
    rb_hash_aset(
        env, SERVER_PORT,
        rb_enc_str_new(pos, tmp.len - (pos - tmp.data), IodineBinaryEncoding));
  }

  /* remove special headers */
  {
    static uint64_t content_length_hash = 0;
    if (!content_length_hash)
      content_length_hash = fiobj_hash_string("content-length", 14);
    FIOBJ cl = fiobj_hash_get2(h->headers, content_length_hash);
    if (cl) {
      tmp = fiobj_obj2cstr(fiobj_hash_get2(h->headers, content_length_hash));
      if (tmp.data) {
        rb_hash_aset(env, CONTENT_LENGTH,
                     rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
        fiobj_hash_delete2(h->headers, content_length_hash);
      }
    }
  }
  {
    static uint64_t content_type_hash = 0;
    if (!content_type_hash)
      content_type_hash = fiobj_hash_string("content-type", 12);
    FIOBJ ct = fiobj_hash_get2(h->headers, content_type_hash);
    if (ct) {
      tmp = fiobj_obj2cstr(ct);
      if (tmp.len && tmp.data) {
        rb_hash_aset(env, CONTENT_TYPE,
                     rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
        fiobj_hash_delete2(h->headers, content_type_hash);
      }
    }
  }
  /* handle scheme / sepcial forwarding headers */
  {
    FIOBJ objtmp;
    static uint64_t xforward_hash = 0;
    if (!xforward_hash)
      xforward_hash = fiobj_hash_string("x-forwarded-proto", 27);
    static uint64_t forward_hash = 0;
    if (!forward_hash)
      forward_hash = fiobj_hash_string("forwarded", 9);
    if ((objtmp = fiobj_hash_get2(h->headers, xforward_hash))) {
      tmp = fiobj_obj2cstr(objtmp);
      if (tmp.len >= 5 && !strncasecmp(tmp.data, "https", 5)) {
        rb_hash_aset(env, R_URL_SCHEME, HTTPS_SCHEME);
      } else if (tmp.len == 4 && !strncasecmp(tmp.data, "http", 4)) {
        rb_hash_aset(env, R_URL_SCHEME, HTTP_SCHEME);
      } else {
        rb_hash_aset(env, R_URL_SCHEME,
                     rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
      }
    } else if ((objtmp = fiobj_hash_get2(h->headers, forward_hash))) {
      tmp = fiobj_obj2cstr(objtmp);
      pos = tmp.data;
      if (pos) {
        while (*pos) {
          if (((*(pos++) | 32) == 'p') && ((*(pos++) | 32) == 'r') &&
              ((*(pos++) | 32) == 'o') && ((*(pos++) | 32) == 't') &&
              ((*(pos++) | 32) == 'o') && ((*(pos++) | 32) == '=')) {
            if ((pos[0] | 32) == 'h' && (pos[1] | 32) == 't' &&
                (pos[2] | 32) == 't' && (pos[3] | 32) == 'p') {
              if ((pos[4] | 32) == 's') {
                rb_hash_aset(env, R_URL_SCHEME, HTTPS_SCHEME);
              } else {
                rb_hash_aset(env, R_URL_SCHEME, HTTP_SCHEME);
              }
            } else {
              char *tmp = pos;
              while (*tmp && *tmp != ';')
                tmp++;
              rb_hash_aset(env, R_URL_SCHEME, rb_str_new(pos, tmp - pos));
            }
            break;
          }
        }
      }
    } else if (http_settings(h)->tls) {
      /* no forwarding information, but we do have TLS */
      rb_hash_aset(env, R_URL_SCHEME, HTTPS_SCHEME);
    } else {
      /* no TLS, no forwarding, assume `http`, which is the default */
    }
  }

  /* add all remaining headers */
  fiobj_each1(h->headers, 0, iodine_copy2env_task, (void *)env);
  return env;
}
#undef add_str_to_env
#undef add_value_to_env
#undef add_header_to_env

/* *****************************************************************************
Handling the HTTP response
***************************************************************************** */

// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static int for_each_header_data(VALUE key, VALUE val, VALUE h_) {
  http_s *h = (http_s *)h_;
  // fprintf(stderr, "For_each - headers\n");
  if (TYPE(key) != T_STRING)
    key = IodineCaller.call(key, iodine_to_s_id);
  if (TYPE(key) != T_STRING)
    return ST_CONTINUE;
  if (TYPE(val) != T_STRING) {
    val = IodineCaller.call(val, iodine_to_s_id);
    if (TYPE(val) != T_STRING)
      return ST_STOP;
  }
  char *key_s = RSTRING_PTR(key);
  int key_len = RSTRING_LEN(key);
  char *val_s = RSTRING_PTR(val);
  int val_len = RSTRING_LEN(val);
  // make the headers lowercase

  FIOBJ name = fiobj_str_new(key_s, key_len);
  {
    fio_str_info_s tmp = fiobj_obj2cstr(name);
    for (int i = 0; i < key_len; ++i) {
      tmp.data[i] = tolower(tmp.data[i]);
    }
  }
  // scan the value for newline (\n) delimiters
  char *pos_s = val_s;
  char *pos_e = val_s + val_len;
  while (pos_s < pos_e) {
    // scanning for newline (\n) delimiters
    char *const start = pos_s;
    pos_s = memchr(pos_s, '\n', pos_e - pos_s);
    if (!pos_s)
      pos_s = pos_e;
    http_set_header(h, name, fiobj_str_new(start, (pos_s - start)));
    // move forward (skip the '\n' if exists)
    ++pos_s;
  }
  fiobj_free(name);
  // no errors, return 0
  return ST_CONTINUE;
}

// writes the body to the response object
static VALUE for_each_body_string(VALUE str, VALUE body_, int argc,
                                  VALUE *argv) {
  // fprintf(stderr, "For_each - body\n");
  // write body
  if (TYPE(str) != T_STRING) {
    FIO_LOG_ERROR("(Iodine) response body not a String\n");
    return Qfalse;
  }
  if (RSTRING_LEN(str) && RSTRING_PTR(str)) {
    fiobj_str_write((FIOBJ)body_, RSTRING_PTR(str), RSTRING_LEN(str));
  }
  return Qtrue;
  (void)argc;
  (void)argv;
}

static inline int ruby2c_response_send(iodine_http_request_handle_s *handle,
                                       VALUE rbresponse, VALUE env) {
  (void)(env);
  VALUE body = rb_ary_entry(rbresponse, 2);
  if (handle->h->status < 200 || handle->h->status == 204 ||
      handle->h->status == 304) {
    if (body && rb_respond_to(body, close_method_id))
      IodineCaller.call(body, close_method_id);
    body = Qnil;
    handle->type = IODINE_HTTP_NONE;
    return 0;
  }
  if (TYPE(body) == T_ARRAY) {
    if (RARRAY_LEN(body) == 0) { // only headers
      handle->type = IODINE_HTTP_EMPTY;
      return 0;
    } else if (RARRAY_LEN(body) == 1) { // [String] is likely
      body = rb_ary_entry(body, 0);
      // fprintf(stderr, "Body was a single item array, unpacket to string\n");
    }
  }

  if (TYPE(body) == T_STRING) {
    // fprintf(stderr, "Review body as String\n");
    handle->type = IODINE_HTTP_NONE;
    if (RSTRING_LEN(body)) {
      handle->body = fiobj_str_new(RSTRING_PTR(body), RSTRING_LEN(body));
      handle->type = IODINE_HTTP_SENDBODY;
    }
    return 0;
  } else if (rb_respond_to(body, each_method_id)) {
    // fprintf(stderr, "Review body as for-each ...\n");
    handle->body = fiobj_str_buf(1);
    handle->type = IODINE_HTTP_SENDBODY;
    IodineCaller.call_with_block(body, each_method_id, 0, NULL,
                                 (VALUE)handle->body, for_each_body_string);
    // we need to call `close` in case the object is an IO / BodyProxy
    if (rb_respond_to(body, close_method_id))
      IodineCaller.call(body, close_method_id);
    return 0;
  }
  return -1;
}

/* *****************************************************************************
Handling Upgrade cases
***************************************************************************** */

static inline int ruby2c_review_upgrade(iodine_http_request_handle_s *req,
                                        VALUE rbresponse, VALUE env) {
  http_s *h = req->h;
  VALUE handler;
  if ((handler = rb_hash_aref(env, IODINE_R_HIJACK_CB)) != Qnil) {
    // send headers
    http_finish(h);
    // call the callback
    VALUE io_ruby = IodineCaller.call(rb_hash_aref(env, IODINE_R_HIJACK),
                                      iodine_call_proc_id);
    IodineCaller.call2(handler, iodine_call_proc_id, 1, &io_ruby);
    goto upgraded;
  } else if ((handler = rb_hash_aref(env, IODINE_R_HIJACK_IO)) != Qnil) {
    //  do nothing, just cleanup
    goto upgraded;
  } else if ((handler = rb_hash_aref(env, UPGRADE_TCP)) != Qnil) {
    goto tcp_ip_upgrade;
  } else {
    switch (req->upgrade) {
    case IODINE_UPGRADE_WEBSOCKET:
      if ((handler = rb_hash_aref(env, RACK_UPGRADE)) != Qnil) {
        // use response as existing base for native websocket upgrade
        iodine_ws_attach(h, handler, env);
        goto upgraded;
      }
      break;
    case IODINE_UPGRADE_SSE:
      if ((handler = rb_hash_aref(env, RACK_UPGRADE)) != Qnil) {
        // use response as existing base for SSE upgrade
        iodine_sse_attach(h, handler, env);
        goto upgraded;
      }
      break;
    default:
      if ((handler = rb_hash_aref(env, RACK_UPGRADE)) != Qnil) {
      tcp_ip_upgrade : {
        // use response as existing base for raw TCP/IP upgrade
        intptr_t uuid = http_hijack(h, NULL);
        // send headers
        http_finish(h);
        // upgrade protocol to raw TCP/IP
        iodine_tcp_attch_uuid(uuid, handler);
        goto upgraded;
      }
      }
      break;
    }
  }
  return 0;

upgraded:
  // get body object to close it (if needed)
  handler = rb_ary_entry(rbresponse, 2);
  // we need to call `close` in case the object is an IO / BodyProxy
  if (handler != Qnil && rb_respond_to(handler, close_method_id))
    IodineCaller.call(handler, close_method_id);
  return 1;
}

/* *****************************************************************************
Handling HTTP requests
***************************************************************************** */

static inline void *iodine_handle_request_in_GVL(void *handle_) {
  iodine_http_request_handle_s *handle = handle_;
  VALUE rbresponse = 0;
  VALUE env = 0;
  http_s *h = handle->h;
  if (!h->udata)
    goto err_not_found;

  // create / register env variable
  env = copy2env(handle);
  // create rack.io
  VALUE tmp = IodineRackIO.create(h, env);
  // pass env variable to handler
  rbresponse =
      IodineCaller.call2((VALUE)h->udata, iodine_call_proc_id, 1, &env);
  // close rack.io
  IodineRackIO.close(tmp);
  // test handler's return value
  if (rbresponse == 0 || rbresponse == Qnil || TYPE(rbresponse) != T_ARRAY)
    goto internal_error;
  IodineStore.add(rbresponse);

  // set response status
  tmp = rb_ary_entry(rbresponse, 0);
  if (TYPE(tmp) == T_STRING) {
    char *data = RSTRING_PTR(tmp);
    h->status = fio_atol(&data);
  } else if (TYPE(tmp) == T_FIXNUM) {
    h->status = FIX2ULONG(tmp);
  } else {
    goto internal_error;
  }

  // handle header copy from ruby land to C land.
  VALUE response_headers = rb_ary_entry(rbresponse, 1);
  if (TYPE(response_headers) != T_HASH)
    goto internal_error;
  // extract the X-Sendfile header (never show original path)
  // X-Sendfile support only present when iodine serves static files.
  VALUE xfiles;
  if (support_xsendfile &&
      (xfiles = rb_hash_aref(response_headers, XSENDFILE)) != Qnil &&
      TYPE(xfiles) == T_STRING) {
    if (OBJ_FROZEN(response_headers)) {
      response_headers = rb_hash_dup(response_headers);
    }
    IodineStore.add(response_headers);
    handle->body = fiobj_str_new(RSTRING_PTR(xfiles), RSTRING_LEN(xfiles));
    handle->type = IODINE_HTTP_XSENDFILE;
    rb_hash_delete(response_headers, XSENDFILE);
    // remove content length headers, as this will be controled by iodine
    rb_hash_delete(response_headers, CONTENT_LENGTH_HEADER);
    // review each header and write it to the response.
    rb_hash_foreach(response_headers, for_each_header_data, (VALUE)(h));
    IodineStore.remove(response_headers);
    // send the file directly and finish
    goto finish;
  }
  // review each header and write it to the response.
  rb_hash_foreach(response_headers, for_each_header_data, (VALUE)(h));
  // review for upgrade.
  if ((intptr_t)h->status < 300 &&
      ruby2c_review_upgrade(handle, rbresponse, env))
    goto external_done;
  // send the request body.
  if (ruby2c_response_send(handle, rbresponse, env))
    goto internal_error;

finish:
  IodineStore.remove(rbresponse);
  IodineStore.remove(env);
  return NULL;

external_done:
  IodineStore.remove(rbresponse);
  IodineStore.remove(env);
  handle->type = IODINE_HTTP_NONE;
  return NULL;

err_not_found:
  IodineStore.remove(rbresponse);
  IodineStore.remove(env);
  h->status = 404;
  handle->type = IODINE_HTTP_ERROR;
  return NULL;

internal_error:
  IodineStore.remove(rbresponse);
  IodineStore.remove(env);
  h->status = 500;
  handle->type = IODINE_HTTP_ERROR;
  return NULL;
}

static inline void
iodine_perform_handle_action(iodine_http_request_handle_s handle) {
  switch (handle.type) {
  case IODINE_HTTP_SENDBODY: {
    fio_str_info_s data = fiobj_obj2cstr(handle.body);
    http_send_body(handle.h, data.data, data.len);
    fiobj_free(handle.body);
    break;
  }
  case IODINE_HTTP_XSENDFILE: {
    /* remove chunked content-encoding header, if any (Rack issue #1266) */
    if (fiobj_obj2cstr(
            fiobj_hash_get2(handle.h->private_data.out_headers,
                            fiobj_obj2hash(HTTP_HEADER_CONTENT_ENCODING)))
            .len == 7)
      fiobj_hash_delete2(handle.h->private_data.out_headers,
                         fiobj_obj2hash(HTTP_HEADER_CONTENT_ENCODING));
    fio_str_info_s data = fiobj_obj2cstr(handle.body);
    if (http_sendfile2(handle.h, data.data, data.len, NULL, 0)) {
      http_send_error(handle.h, 404);
    }
    fiobj_free(handle.body);
    break;
  }
  case IODINE_HTTP_EMPTY:
    http_finish(handle.h);
    fiobj_free(handle.body);
    break;
  case IODINE_HTTP_NONE:
    /* nothing to do - this had to be performed within the Ruby GIL :-( */
    break;
  case IODINE_HTTP_ERROR:
    http_send_error(handle.h, handle.h->status);
    fiobj_free(handle.body);
    break;
  }
}
static void on_rack_request(http_s *h) {
  iodine_http_request_handle_s handle = (iodine_http_request_handle_s){
      .h = h,
      .upgrade = IODINE_UPGRADE_NONE,
  };
  IodineCaller.enterGVL((void *(*)(void *))iodine_handle_request_in_GVL,
                        &handle);
  iodine_perform_handle_action(handle);
}

static void on_rack_upgrade(http_s *h, char *proto, size_t len) {
  iodine_http_request_handle_s handle = (iodine_http_request_handle_s){.h = h};
  if (len == 9 && (proto[1] == 'e' || proto[1] == 'E')) {
    handle.upgrade = IODINE_UPGRADE_WEBSOCKET;
  } else if (len == 3 && proto[0] == 's') {
    handle.upgrade = IODINE_UPGRADE_SSE;
  }
  /* when we stop supporting custom Upgrade headers: */
  // else {
  //   http_send_error(h, 400);
  //   return;
  // }
  IodineCaller.enterGVL(iodine_handle_request_in_GVL, &handle);
  iodine_perform_handle_action(handle);
  (void)proto;
  (void)len;
}

/* *****************************************************************************
Rack `env` Template Initialization
***************************************************************************** */

static void initialize_env_template(void) {
  if (env_template_no_upgrade)
    return;
  env_template_no_upgrade = rb_hash_new();
  IodineStore.add(env_template_no_upgrade);

#define add_str_to_env(env, key, value)                                        \
  {                                                                            \
    VALUE k = rb_enc_str_new((key), strlen((key)), IodineBinaryEncoding);      \
    rb_obj_freeze(k);                                                          \
    VALUE v = rb_enc_str_new((value), strlen((value)), IodineBinaryEncoding);  \
    rb_obj_freeze(v);                                                          \
    rb_hash_aset(env, k, v);                                                   \
  }
#define add_value_to_env(env, key, value)                                      \
  {                                                                            \
    VALUE k = rb_enc_str_new((key), strlen((key)), IodineBinaryEncoding);      \
    rb_obj_freeze(k);                                                          \
    rb_hash_aset((env), k, value);                                             \
  }

  /* Set global template */
  rb_hash_aset(env_template_no_upgrade, RACK_UPGRADE_Q, Qnil);
  rb_hash_aset(env_template_no_upgrade, RACK_UPGRADE, Qnil);
  {
    /* add the rack.version */
    static VALUE rack_version = 0;
    if (!rack_version) {
      rack_version = rb_ary_new(); // rb_ary_new is Ruby 2.0 compatible
      rb_ary_push(rack_version, INT2FIX(1));
      rb_ary_push(rack_version, INT2FIX(3));
      rb_global_variable(&rack_version);
      rb_ary_freeze(rack_version);
    }
    add_value_to_env(env_template_no_upgrade, "rack.version", rack_version);
  }

  {
    const char *sn = getenv("SCRIPT_NAME");
    if (!sn || (sn[0] == '/' && sn[1] == 0)) {
      sn = "";
    }
    add_str_to_env(env_template_no_upgrade, "SCRIPT_NAME", sn);
  }
  rb_hash_aset(env_template_no_upgrade, IODINE_R_INPUT, IODINE_R_INPUT_DEFAULT);
  add_value_to_env(env_template_no_upgrade, "rack.errors", rb_stderr);
  add_value_to_env(env_template_no_upgrade, "rack.hijack?", Qtrue);
  add_value_to_env(env_template_no_upgrade, "rack.multiprocess", Qtrue);
  add_value_to_env(env_template_no_upgrade, "rack.multithread", Qtrue);
  add_value_to_env(env_template_no_upgrade, "rack.run_once", Qfalse);
  /* default schema to http, it might be updated later */
  rb_hash_aset(env_template_no_upgrade, R_URL_SCHEME, HTTP_SCHEME);
  /* placeholders... minimize rehashing*/
  rb_hash_aset(env_template_no_upgrade, HTTP_VERSION, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, IODINE_R_HIJACK, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, PATH_INFO, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, QUERY_STRING, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, REMOTE_ADDR, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, REQUEST_METHOD, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, SERVER_NAME, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, SERVER_PORT, QUERY_ESTRING);
  rb_hash_aset(env_template_no_upgrade, SERVER_PROTOCOL, QUERY_STRING);

  /* WebSocket upgrade support */
  env_template_websockets = rb_hash_dup(env_template_no_upgrade);
  IodineStore.add(env_template_websockets);
  rb_hash_aset(env_template_websockets, RACK_UPGRADE_Q, RACK_UPGRADE_WEBSOCKET);

  /* SSE upgrade support */
  env_template_sse = rb_hash_dup(env_template_no_upgrade);
  IodineStore.add(env_template_sse);
  rb_hash_aset(env_template_sse, RACK_UPGRADE_Q, RACK_UPGRADE_SSE);

#undef add_value_to_env
#undef add_str_to_env
}

/* *****************************************************************************
Listenninng to HTTP
*****************************************************************************
*/

static void free_iodine_http(http_settings_s *s) {
  IodineStore.remove((VALUE)s->udata);
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
intptr_t iodine_http_listen(iodine_connection_args_s args){
  // clang-format on
  if (args.public.data) {
    rb_hash_aset(env_template_no_upgrade, XSENDFILE_TYPE, XSENDFILE);
    rb_hash_aset(env_template_no_upgrade, XSENDFILE_TYPE_HEADER, XSENDFILE);
    support_xsendfile = 1;
  }
  IodineStore.add(args.handler);
  intptr_t uuid = http_listen(
      args.port.data, args.address.data, .on_request = on_rack_request,
      .on_upgrade = on_rack_upgrade, .udata = (void *)args.handler,
      .tls = args.tls, .timeout = args.timeout, .ws_timeout = args.ping,
      .ws_max_msg_size = args.max_msg, .max_header_size = args.max_headers,
      .on_finish = free_iodine_http, .log = args.log,
      .max_body_size = args.max_body, .public_folder = args.public.data);
  if (uuid == -1)
    return uuid;

  if ((args.handler == Qnil || args.handler == Qfalse)) {
    FIO_LOG_WARNING("(listen) no handler / app, the HTTP service on port %s "
                    "will only serve "
                    "static files.",
                    args.port.data ? args.port.data : args.address.data);
  }
  if (args.public.data) {
    FIO_LOG_INFO("Serving static files from %s", args.public.data);
  }

  return uuid;
}

/* *****************************************************************************
HTTP Websocket Connect
***************************************************************************** */

typedef struct {
  FIOBJ method;
  FIOBJ headers;
  FIOBJ cookies;
  FIOBJ body;
  VALUE io;
} request_data_s;

static request_data_s *request_data_create(iodine_connection_args_s *args) {
  request_data_s *r = fio_malloc(sizeof(*r));
  FIO_ASSERT_ALLOC(r);
  VALUE io =
      iodine_connection_new(.type = IODINE_CONNECTION_WEBSOCKET, .arg = NULL,
                            .handler = args->handler, .env = Qnil, .uuid = 0);

  *r = (request_data_s){
      .method = fiobj_str_new(args->method.data, args->method.len),
      .headers = fiobj_dup(args->headers),
      .cookies = fiobj_dup(args->cookies),
      .body = fiobj_str_new(args->body.data, args->body.len),
      .io = io,
  };
  return r;
}

static void request_data_destroy(request_data_s *r) {
  fiobj_free(r->method);
  fiobj_free(r->body);
  fiobj_free(r->headers);
  fiobj_free(r->cookies);
  fio_free(r);
}

static int each_header_ws_client_task(FIOBJ val, void *h_) {
  http_s *h = h_;
  FIOBJ key = fiobj_hash_key_in_loop();
  http_set_header(h, key, fiobj_dup(val));
  return 0;
}
static int each_cookie_ws_client_task(FIOBJ val, void *h_) {
  http_s *h = h_;
  FIOBJ key = fiobj_hash_key_in_loop();
  fio_str_info_s n = fiobj_obj2cstr(key);
  fio_str_info_s v = fiobj_obj2cstr(val);
  http_set_cookie(h, .name = n.data, .name_len = n.len, .value = v.data,
                  .value_len = v.len);
  return 0;
}

static void ws_client_http_connected(http_s *h) {
  request_data_s *s = h->udata;
  if (!s)
    return;
  h->udata = http_settings(h)->udata = NULL;
  if (!h->path) {
    h->path = fiobj_str_new("/", 1);
  }
  /* TODO: add headers and cookies */
  fiobj_each1(s->headers, 0, each_header_ws_client_task, h);
  fiobj_each1(s->headers, 0, each_cookie_ws_client_task, h);
  if (s->io && s->io != Qnil)
    http_upgrade2ws(
        h, .on_message = iodine_ws_on_message, .on_open = iodine_ws_on_open,
        .on_ready = iodine_ws_on_ready, .on_shutdown = iodine_ws_on_shutdown,
        .on_close = iodine_ws_on_close, .udata = (void *)s->io);
  request_data_destroy(s);
}

static void ws_client_http_connection_finished(http_settings_s *settings) {
  if (!settings)
    return;
  request_data_s *s = settings->udata;
  if (s) {
    if (s->io && s->io != Qnil)
      iodine_connection_fire_event(s->io, IODINE_CONNECTION_ON_CLOSE, Qnil);
    request_data_destroy(s);
  }
}

/** Connects to a (remote) WebSocket service. */
intptr_t iodine_ws_connect(iodine_connection_args_s args) {
  // http_connect(url, unixaddr, struct http_settings_s)
  uint8_t is_unix_socket = 0;
  if (memchr(args.address.data, '/', args.address.len)) {
    is_unix_socket = 1;
  }
  FIOBJ url_tmp = FIOBJ_INVALID;
  if (!args.url.data) {
    url_tmp = fiobj_str_buf(64);
    if (args.tls)
      fiobj_str_write(url_tmp, "wss://", 6);
    else
      fiobj_str_write(url_tmp, "ws://", 5);
    if (!is_unix_socket) {
      fiobj_str_write(url_tmp, args.address.data, args.address.len);
      if (args.port.data) {
        fiobj_str_write(url_tmp, ":", 1);
        fiobj_str_write(url_tmp, args.port.data, args.port.len);
      }
    }
    if (args.path.data)
      fiobj_str_write(url_tmp, args.path.data, args.path.len);
    else
      fiobj_str_write(url_tmp, "/", 1);
    args.url = fiobj_obj2cstr(url_tmp);
  }

  intptr_t uuid = http_connect(
      args.url.data, (is_unix_socket ? args.address.data : NULL),
      .udata = request_data_create(&args),
      .on_response = ws_client_http_connected,
      .on_finish = ws_client_http_connection_finished, .tls = args.tls);
  fiobj_free(url_tmp);
  return uuid;
}

/* *****************************************************************************
Initialization
***************************************************************************** */

void iodine_init_http(void) {

  rack_autoset(REQUEST_METHOD);
  rack_autoset(PATH_INFO);
  rack_autoset(QUERY_STRING);
  rack_autoset(SERVER_NAME);
  rack_autoset(SERVER_PORT);
  rack_autoset(CONTENT_LENGTH);
  rack_autoset(CONTENT_TYPE);
  rack_autoset(SERVER_PROTOCOL);
  rack_autoset(HTTP_VERSION);
  rack_autoset(REMOTE_ADDR);

  rack_autoset(HTTP_ACCEPT);
  rack_autoset(HTTP_USER_AGENT);
  rack_autoset(HTTP_ACCEPT_ENCODING);
  rack_autoset(HTTP_ACCEPT_LANGUAGE);
  rack_autoset(HTTP_CONNECTION);
  rack_autoset(HTTP_HOST);

  rack_set(HTTP_SCHEME, "http");
  rack_set(HTTPS_SCHEME, "https");
  rack_set(QUERY_ESTRING, "");
  rack_set(R_URL_SCHEME, "rack.url_scheme");
  rack_set(R_INPUT, "rack.input");
  rack_set(XSENDFILE, "X-Sendfile");
  rack_set(XSENDFILE_TYPE, "sendfile.type");
  rack_set(XSENDFILE_TYPE_HEADER, "HTTP_X_SENDFILE_TYPE");
  rack_set(CONTENT_LENGTH_HEADER, "Content-Length");

  rack_set(IODINE_R_INPUT, "rack.input");
  rack_set(IODINE_R_HIJACK_IO, "rack.hijack_io");
  rack_set(IODINE_R_HIJACK, "rack.hijack");
  rack_set(IODINE_R_HIJACK_CB, "iodine.hijack_cb");

  rack_set(RACK_UPGRADE, "rack.upgrade");
  rack_set(RACK_UPGRADE_Q, "rack.upgrade?");
  rack_set_sym(RACK_UPGRADE_SSE, "sse");
  rack_set_sym(RACK_UPGRADE_WEBSOCKET, "websocket");

  UPGRADE_TCP = IodineStore.add(rb_str_new("upgrade.tcp", 11));

  hijack_func_sym = ID2SYM(rb_intern("_hijack"));
  close_method_id = rb_intern("close");
  each_method_id = rb_intern("each");
  attach_method_id = rb_intern("attach_fd");
  iodine_call_proc_id = rb_intern("call");

  IodineUTF8Encoding = rb_enc_find("UTF-8");
  IodineBinaryEncoding = rb_enc_find("binary");

  {
    VALUE STRIO_CLASS = rb_const_get(rb_cObject, rb_intern("StringIO"));
    IODINE_R_INPUT_DEFAULT = rb_str_new_static("", 0);
    IODINE_R_INPUT_DEFAULT =
        rb_funcallv(STRIO_CLASS, rb_intern("new"), 1, &IODINE_R_INPUT_DEFAULT);
    rb_global_variable(&IODINE_R_INPUT_DEFAULT);
  }
  initialize_env_template();
}
