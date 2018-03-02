/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_http.h"
#include "http.h"
#include "iodine_json.h"
#include "iodine_websockets.h"
#include "rb-rack-io.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/* *****************************************************************************
Available Globals
***************************************************************************** */
VALUE IodineHTTP;

typedef struct {
  VALUE app;
  VALUE env;
} iodine_http_settings_s;

/* these three are used also by rb-rack-io.c */
VALUE IODINE_R_HIJACK;
VALUE IODINE_R_HIJACK_IO;
VALUE IODINE_R_HIJACK_CB;

VALUE UPGRADE_TCP;
VALUE UPGRADE_TCP_Q;
VALUE UPGRADE_WEBSOCKET;
VALUE UPGRADE_WEBSOCKET_Q;

static VALUE hijack_func_sym;
static ID to_fixnum_func_id;
static ID close_method_id;
static ID each_method_id;
static ID attach_method_id;

static VALUE env_template_no_upgrade;
static VALUE env_template_with_upgrade;

#define rack_declare(rack_name) static VALUE rack_name

#define rack_set(rack_name, str)                                               \
  (rack_name) = rb_enc_str_new((str), strlen((str)), IodineBinaryEncoding);    \
  rb_global_variable(&(rack_name));                                            \
  rb_obj_freeze(rack_name);

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
rack_declare(CONTENT_LENGTH_HEADER); // for X-Sendfile support

/* *****************************************************************************
Copying data from the C request to the Rack's ENV
***************************************************************************** */

#define to_upper(c) (((c) >= 'a' && (c) <= 'z') ? ((c) & ~32) : (c))

int iodine_copy2env_task(FIOBJ o, void *env_) {
  VALUE env = (VALUE)env_;
  FIOBJ name = fiobj_hash_key_in_loop();
  fio_cstr_s tmp = fiobj_obj2cstr(name);
  VALUE hname = rb_str_buf_new(6 + tmp.len);
  {
    memcpy(RSTRING_PTR(hname), "HTTP_", 5);
    char *pos = RSTRING_PTR(hname) + 5;
    char *reader = tmp.data;
    while (*reader) {
      *(pos++) = *reader == '-' ? '_' : to_upper(*reader);
      ++reader;
    }
    *pos = 0;
    rb_str_set_len(hname, 5 + tmp.len);
  }
  if (FIOBJ_TYPE_IS(o, FIOBJ_T_STRING)) {
    tmp = fiobj_obj2cstr(o);
    rb_hash_aset(env, hname,
                 rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));

  } else {
    /* it's an array */
    VALUE ary = rb_ary_new();
    rb_hash_aset(env, hname, ary);
    size_t count = fiobj_ary_count(o);
    for (size_t i = 0; i < count; ++i) {
      tmp = fiobj_obj2cstr(fiobj_ary_index(o, i));
      rb_ary_push(ary, rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
    }
  }
  return 0;
}
static inline VALUE copy2env(http_s *h, uint8_t is_upgrade) {
  VALUE env = rb_hash_dup(is_upgrade ? env_template_with_upgrade
                                     : env_template_no_upgrade);
  Registry.add(env);

  fio_cstr_s tmp;
  char *pos = NULL;
  /* Copy basic data */
  tmp = fiobj_obj2cstr(h->method);
  rb_hash_aset(env, REQUEST_METHOD,
               rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
  tmp = fiobj_obj2cstr(h->path);
  rb_hash_aset(env, PATH_INFO,
               rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
  tmp = fiobj_obj2cstr(h->query);
  rb_hash_aset(env, QUERY_STRING,
               tmp.len ? rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding)
                       : QUERY_ESTRING);
  {
    // HTTP version appears twice
    tmp = fiobj_obj2cstr(h->version);
    VALUE hname = rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding);
    rb_hash_aset(env, SERVER_PROTOCOL, hname);
    rb_hash_aset(env, HTTP_VERSION, hname);
  }

  { // Support for Ruby web-console.
    VALUE address = rb_str_buf_new(64);
    sock_peer_addr_s addrinfo = http_peer_addr(h);
    if (addrinfo.addrlen &&
        inet_ntop(
            addrinfo.addr->sa_family,
            addrinfo.addr->sa_family == AF_INET
                ? (void *)&((struct sockaddr_in *)addrinfo.addr)->sin_addr
                : (void *)&((struct sockaddr_in6 *)addrinfo.addr)->sin6_addr,
            RSTRING_PTR(address), 64)) {
      rb_str_set_len(address, strlen(RSTRING_PTR(address)));
      rb_hash_aset(env, REMOTE_ADDR, address);
    }
  }

  /* setup input IO + hijack support */
  {
    VALUE m;
    rb_hash_aset(env, R_INPUT, (m = IodineRackIO.create(h, env)));
    m = rb_obj_method(m, hijack_func_sym);
    rb_hash_aset(env, IODINE_R_HIJACK, m);
  }

  /* handle the HOST header, including the possible host:#### format*/
  static uint64_t host_hash = 0;
  if (!host_hash)
    host_hash = fio_siphash("host", 4);
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
  static uint64_t content_length_hash = 0;
  if (!content_length_hash)
    content_length_hash = fio_siphash("content-length", 14);
  tmp = fiobj_obj2cstr(fiobj_hash_get2(h->headers, content_length_hash));
  if (tmp.data) {
    rb_hash_aset(env, CONTENT_LENGTH,
                 rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
    fiobj_hash_delete2(h->headers, content_length_hash);
  }
  static uint64_t content_type_hash = 0;
  if (!content_type_hash)
    content_length_hash = fio_siphash("content-type", 12);
  tmp = fiobj_obj2cstr(fiobj_hash_get2(h->headers, content_type_hash));
  if (tmp.data) {
    rb_hash_aset(env, CONTENT_TYPE,
                 rb_enc_str_new(tmp.data, tmp.len, IodineBinaryEncoding));
    fiobj_hash_delete2(h->headers, content_type_hash);
  }

  /* handle scheme / sepcial forwarding headers */
  {
    FIOBJ objtmp;
    static uint64_t xforward_hash = 0;
    if (!xforward_hash)
      xforward_hash = fio_siphash("x-forwarded-proto", 27);
    static uint64_t forward_hash = 0;
    if (!forward_hash)
      forward_hash = fio_siphash("forwarded", 9);
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
    } else {
    }
  }

  /* add all remianing headers */
  fiobj_each1(h->headers, 0, iodine_copy2env_task, (void *)env);
  return env;
}
#undef add_str_to_env
#undef add_value_to_env
#undef add_header_to_env

/* *****************************************************************************
Handling the HTTP response
***************************************************************************** */

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
} iodine_http_request_handle_s;

// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static int for_each_header_data(VALUE key, VALUE val, VALUE h_) {
  http_s *h = (http_s *)h_;
  // fprintf(stderr, "For_each - headers\n");
  if (TYPE(key) != T_STRING)
    key = RubyCaller.call(key, iodine_to_s_method_id);
  if (TYPE(key) != T_STRING)
    return ST_CONTINUE;
  if (TYPE(val) != T_STRING) {
    val = RubyCaller.call(val, iodine_to_s_method_id);
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
    fio_cstr_s tmp = fiobj_obj2cstr(name);
    for (int i = 0; i < key_len; ++i) {
      tmp.data[i] = tolower(tmp.data[i]);
    }
  }
  // scan the value for newline (\n) delimiters
  int pos_s = 0, pos_e = 0;
  while (pos_e < val_len) {
    // scanning for newline (\n) delimiters
    while (pos_e < val_len && val_s[pos_e] != '\n')
      pos_e++;
    http_set_header(h, name, fiobj_str_new(val_s + pos_s, pos_e - pos_s));
    // fprintf(stderr, "For_each - headers: wrote header\n");
    // move forward (skip the '\n' if exists)
    pos_s = pos_e + 1;
    pos_e++;
  }
  fiobj_free(name);
  // no errors, return 0
  return ST_CONTINUE;
}

// writes the body to the response object
static VALUE for_each_body_string(VALUE str, VALUE body_) {
  // fprintf(stderr, "For_each - body\n");
  // write body
  if (TYPE(str) != T_STRING) {
    fprintf(stderr, "Iodine Server Error:"
                    "response body was not a String\n");
    return Qfalse;
  }
  if (RSTRING_LEN(str) && RSTRING_PTR(str)) {
    fiobj_str_write((FIOBJ)body_, RSTRING_PTR(str), RSTRING_LEN(str));
  }
  return Qtrue;
}

static inline int ruby2c_response_send(iodine_http_request_handle_s *handle,
                                       VALUE rbresponse, VALUE env) {
  (void)(env);
  VALUE body = rb_ary_entry(rbresponse, 2);
  if (handle->h->status < 200 || handle->h->status == 204 ||
      handle->h->status == 304) {
    if (rb_respond_to(body, close_method_id))
      RubyCaller.call(body, close_method_id);
    body = Qnil;
    handle->type = IODINE_HTTP_NONE;
    return 0;
  }
  if (TYPE(body) == T_ARRAY) {
    if (RARRAY_LEN(body) == 0) { // only headers
      handle->type = IODINE_HTTP_EMPTY;
    } else if (RARRAY_LEN(body) == 1) { // [String] is likely
      body = rb_ary_entry(body, 0);
      // fprintf(stderr, "Body was a single item array, unpacket to string\n");
    }
  }

  if (TYPE(body) == T_STRING) {
    // fprintf(stderr, "Review body as String\n");
    if (RSTRING_LEN(body))
      handle->body = fiobj_str_new(RSTRING_PTR(body), RSTRING_LEN(body));
    handle->type = IODINE_HTTP_SENDBODY;
    return 0;
  } else if (rb_respond_to(body, each_method_id)) {
    // fprintf(stderr, "Review body as for-each ...\n");
    handle->body = fiobj_str_buf(1);
    handle->type = IODINE_HTTP_SENDBODY;
    rb_block_call(body, each_method_id, 0, NULL, for_each_body_string,
                  (VALUE)handle->body);
    // we need to call `close` in case the object is an IO / BodyProxy
    if (rb_respond_to(body, close_method_id))
      RubyCaller.call(body, close_method_id);
    return 0;
  }
  return -1;
}

/* *****************************************************************************
Handling Upgrade cases
***************************************************************************** */

static inline int ruby2c_review_upgrade(http_s *h, VALUE rbresponse,
                                        VALUE env) {
  VALUE handler;
  if ((handler = rb_hash_aref(env, IODINE_R_HIJACK_CB)) != Qnil) {
    // send headers
    http_finish(h);
    // call the callback
    VALUE io_ruby = RubyCaller.call(rb_hash_aref(env, IODINE_R_HIJACK),
                                    iodine_call_proc_id);
    RubyCaller.call2(handler, iodine_call_proc_id, 1, &io_ruby);
  } else if ((handler = rb_hash_aref(env, IODINE_R_HIJACK_IO)) != Qnil) {
    //  do nothing
  } else if ((handler = rb_hash_aref(env, UPGRADE_WEBSOCKET)) != Qnil) {
    // use response as existing base for native websocket upgrade
    iodine_websocket_upgrade(h, handler);
  } else if ((handler = rb_hash_aref(env, UPGRADE_TCP)) != Qnil) {
    // hijack post headers (might be very bad)
    intptr_t uuid = http_hijack(h, NULL);
    // send headers
    http_finish(h);
    // upgrade protocol
    VALUE args[2] = {(ULONG2NUM(sock_uuid2fd(uuid))), handler};
    RubyCaller.call2(Iodine, attach_method_id, 2, args);
    // nothing left to do to prevent response processing.
  } else {
    return 0;
  }
  // get body object to close it (if needed)
  handler = rb_ary_entry(rbresponse, 2);
  // we need to call `close` in case the object is an IO / BodyProxy
  if (handler != Qnil && rb_respond_to(handler, close_method_id))
    RubyCaller.call(handler, close_method_id);
  return 1;
}

/* *****************************************************************************
Handling HTTP requests
***************************************************************************** */

static inline void *
iodine_handle_request_in_GVL(iodine_http_request_handle_s *handle,
                             uint8_t is_upgrade) {
  VALUE rbresponse = 0;
  VALUE env = 0;
  http_s *h = handle->h;
  if (!h->udata)
    goto err_not_found;

  // create / register env variable
  env = copy2env(h, is_upgrade);
  // will be used later
  VALUE tmp;
  // pass env variable to handler
  rbresponse = RubyCaller.call2((VALUE)h->udata, iodine_call_proc_id, 1, &env);
  // test handler's return value
  if (rbresponse == 0 || rbresponse == Qnil)
    goto internal_error;
  Registry.add(rbresponse);

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
  if (http_settings(h)->public_folder &&
      (xfiles = rb_hash_aref(response_headers, XSENDFILE)) != Qnil &&
      TYPE(xfiles) == T_STRING) {
    if (OBJ_FROZEN(response_headers)) {
      response_headers = rb_hash_dup(response_headers);
    }
    Registry.add(response_headers);
    handle->body = fiobj_str_new(RSTRING_PTR(xfiles), RSTRING_LEN(xfiles));
    handle->type = IODINE_HTTP_XSENDFILE;
    rb_hash_delete(response_headers, XSENDFILE);
    // remove content length headers, as this will be controled by iodine
    rb_hash_delete(response_headers, CONTENT_LENGTH_HEADER);
    // review each header and write it to the response.
    rb_hash_foreach(response_headers, for_each_header_data, (VALUE)(h));
    Registry.remove(response_headers);
    // send the file directly and finish
    return NULL;
  }
  // review each header and write it to the response.
  rb_hash_foreach(response_headers, for_each_header_data, (VALUE)(h));
  // review for upgrade.
  if (ruby2c_review_upgrade(h, rbresponse, env))
    goto external_done;
  // send the request body.
  if (ruby2c_response_send(handle, rbresponse, env))
    goto internal_error;

  Registry.remove(rbresponse);
  Registry.remove(env);
  return NULL;

external_done:
  Registry.remove(rbresponse);
  Registry.remove(env);
  handle->type = IODINE_HTTP_NONE;
  return NULL;

err_not_found:
  Registry.remove(rbresponse);
  Registry.remove(env);
  h->status = 404;
  handle->type = IODINE_HTTP_ERROR;
  return NULL;

internal_error:
  Registry.remove(rbresponse);
  Registry.remove(env);
  h->status = 500;
  handle->type = IODINE_HTTP_ERROR;
  return NULL;
}

static void *on_rack_request_in_GVL(void *h_) {
  return iodine_handle_request_in_GVL(h_, 0);
}
static void *on_rack_upgrade_in_GVL(void *h_) {
  return iodine_handle_request_in_GVL(h_, 1);
}

static inline void
iodine_perform_handle_action(iodine_http_request_handle_s handle) {
  switch (handle.type) {
  case IODINE_HTTP_SENDBODY: {
    fio_cstr_s data = fiobj_obj2cstr(handle.body);
    http_send_body(handle.h, data.data, data.len);
    fiobj_free(handle.body);
    break;
  }
  case IODINE_HTTP_XSENDFILE: {
    fio_cstr_s data = fiobj_obj2cstr(handle.body);
    if (http_sendfile2(handle.h, data.data, data.len, NULL, 0))
      http_send_error(handle.h, 404);
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
  iodine_http_request_handle_s handle = (iodine_http_request_handle_s){.h = h};
  RubyCaller.call_c((void *(*)(void *))on_rack_request_in_GVL, &handle);
  iodine_perform_handle_action(handle);
}

static void on_rack_upgrade(http_s *h, char *proto, size_t len) {
  iodine_http_request_handle_s handle = (iodine_http_request_handle_s){.h = h};
  RubyCaller.call_c((void *(*)(void *))on_rack_upgrade_in_GVL, &handle);
  iodine_perform_handle_action(handle);
  (void)proto;
  (void)len;
}

/* *****************************************************************************
Listenninng to HTTP
*****************************************************************************
*/

void *iodine_print_http_msg2_in_gvl(void *d_) {
  // Write message
  struct {
    VALUE www;
    VALUE port;
  } *arg = d_;
  if (arg->www) {
    fprintf(stderr,
            "Iodine HTTP Server on port %s:\n"
            " *    Serving static files from %s\n\n",
            StringValueCStr(arg->port), StringValueCStr(arg->www));
    Registry.remove(arg->www);
  }
  Registry.remove(arg->port);
  return NULL;
}

void *iodine_print_http_msg_in_gvl(void *d_) {
  // Write message
  VALUE iodine_version = rb_const_get(Iodine, rb_intern("VERSION"));
  VALUE ruby_version = rb_const_get(Iodine, rb_intern("RUBY_VERSION"));
  struct {
    VALUE www;
    VALUE port;
  } *arg = d_;
  if (arg->www) {
    fprintf(stderr,
            "\nStarting up Iodine HTTP Server on port %s:\n"
            " * Ruby v.%s\n * Iodine v.%s \n"
            " * %lu max concurrent connections / open files\n"
            " * Serving static files from %s\n\n",
            StringValueCStr(arg->port), StringValueCStr(ruby_version),
            StringValueCStr(iodine_version), (size_t)sock_max_capacity(),
            StringValueCStr(arg->www));
    Registry.remove(arg->www);
  } else
    fprintf(stderr,
            "\nStarting up Iodine HTTP Server on port %s:\n"
            " * Ruby v.%s\n * Iodine v.%s \n"
            " * %lu max concurrent connections / open files\n\n",
            StringValueCStr(arg->port), StringValueCStr(ruby_version),
            StringValueCStr(iodine_version), (size_t)sock_max_capacity());
  Registry.remove(arg->port);

  return NULL;
}

static void iodine_print_http_msg1(void *www, void *port) {
  if (facil_parent_pid() != getpid())
    return;
  struct {
    void *www;
    void *port;
  } data = {.www = www, .port = port};
  RubyCaller.call_c(iodine_print_http_msg_in_gvl, (void *)&data);
}
static void iodine_print_http_msg2(void *www, void *port) {
  if (facil_parent_pid() != getpid())
    return;
  struct {
    void *www;
    void *port;
  } data = {.www = www, .port = port};
  RubyCaller.call_c(iodine_print_http_msg2_in_gvl, (void *)&data);
}

static void free_iodine_http(http_settings_s *s) {
  Registry.remove((VALUE)s->udata);
}
/**
Listens to incoming HTTP connections and handles incoming requests using the
Rack specification.

This is delegated to a lower level C HTTP and Websocket implementation, no
Ruby object will be crated except the `env` object required by the Rack
specifications.

Accepts a single Hash argument with the following properties:

app:: the Rack application that handles incoming requests. Default: `nil`.
port:: the port to listen to. Default: 3000.
address:: the address to bind to. Default: binds to all possible addresses.
log:: enable response logging (Hijacked sockets aren't logged). Default: off.
public:: The root public folder for static file service. Default: none.
timeout:: Timeout for inactive HTTP/1.x connections. Defaults: 5 seconds.
max_body:: The maximum body size for incoming HTTP messages. Default: ~50Mib.
max_msg:: The maximum Websocket message size allowed. Default: ~250Kib.
ping:: The Websocket `ping` interval. Default: 40 sec.

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
VALUE iodine_http_listen(VALUE self, VALUE opt) {
  static int called_once = 0;
  uint8_t log_http = 0;
  size_t ping = 0;
  size_t max_body = 0;
  size_t max_headers = 0;
  size_t max_msg = 0;
  Check_Type(opt, T_HASH);
  VALUE app = rb_hash_aref(opt, ID2SYM(rb_intern("app")));
  VALUE www = rb_hash_aref(opt, ID2SYM(rb_intern("public")));
  VALUE port = rb_hash_aref(opt, ID2SYM(rb_intern("port")));
  VALUE address = rb_hash_aref(opt, ID2SYM(rb_intern("address")));
  VALUE tout = rb_hash_aref(opt, ID2SYM(rb_intern("timeout")));

  VALUE tmp = rb_hash_aref(opt, ID2SYM(rb_intern("max_msg")));
  if (tmp != Qnil && tmp != Qfalse) {
    Check_Type(tmp, T_FIXNUM);
    max_msg = FIX2ULONG(tmp);
  }

  tmp = rb_hash_aref(opt, ID2SYM(rb_intern("max_body")));
  if (tmp != Qnil && tmp != Qfalse) {
    Check_Type(tmp, T_FIXNUM);
    max_body = FIX2ULONG(tmp);
  }
  tmp = rb_hash_aref(opt, ID2SYM(rb_intern("max_headers")));
  if (tmp != Qnil && tmp != Qfalse) {
    Check_Type(tmp, T_FIXNUM);
    max_headers = FIX2ULONG(tmp);
  }

  tmp = rb_hash_aref(opt, ID2SYM(rb_intern("ping")));
  if (tmp != Qnil && tmp != Qfalse) {
    Check_Type(tmp, T_FIXNUM);
    ping = FIX2ULONG(tmp);
  }
  if (ping > 255) {
    fprintf(stderr, "Iodine Warning: Websocket timeout value "
                    "is over 255 and is silently ignored.\n");
    ping = 0;
  }

  tmp = rb_hash_aref(opt, ID2SYM(rb_intern("log")));
  if (tmp != Qnil && tmp != Qfalse)
    log_http = 1;

  if ((app == Qnil || app == Qfalse) && (www == Qnil || www == Qfalse)) {
    fprintf(stderr, "Iodine Warning: HTTP without application or public folder "
                    "(is silently ignored).\n");
    return Qfalse;
  }

  if ((www != Qnil && www != Qfalse)) {
    Check_Type(www, T_STRING);
    Registry.add(www);
  } else
    www = 0;

  if ((address != Qnil && address != Qfalse))
    Check_Type(address, T_STRING);
  else
    address = 0;

  if ((tout != Qnil && tout != Qfalse)) {
    Check_Type(tout, T_FIXNUM);
    tout = FIX2ULONG(tout);
  } else
    tout = 0;
  if (tout > 255) {
    fprintf(stderr, "Iodine Warning: HTTP timeout value "
                    "is over 255 and is silently ignored.\n");
    tout = 0;
  }

  if (port != Qnil && port != Qfalse) {
    if (!RB_TYPE_P(port, T_STRING) && !RB_TYPE_P(port, T_FIXNUM))
      rb_raise(rb_eTypeError,
               "The `port` property MUST be either a String or a Number");
    if (RB_TYPE_P(port, T_FIXNUM))
      port = rb_funcall2(port, iodine_to_s_method_id, 0, NULL);
    Registry.add(port);
  } else if (port == Qfalse)
    port = 0;
  else {
    port = rb_str_new("3000", 4);
    Registry.add(port);
  }

  if ((app != Qnil && app != Qfalse))
    Registry.add(app);
  else
    app = 0;

  if (http_listen(
          StringValueCStr(port), (address ? StringValueCStr(address) : NULL),
          .on_request = on_rack_request, .on_upgrade = on_rack_upgrade,
          .udata = (void *)app, .timeout = (tout ? FIX2INT(tout) : tout),
          .ws_timeout = ping, .ws_max_msg_size = max_msg,
          .max_header_size = max_headers, .on_finish = free_iodine_http,
          .log = log_http, .max_body_size = max_body,
          .public_folder = (www ? StringValueCStr(www) : NULL))) {
    fprintf(stderr,
            "ERROR: Failed to initialize a listening HTTP socket for port %s\n",
            port ? StringValueCStr(port) : "3000");
    return Qfalse;
  }
  Registry.remove(port);
  Registry.remove(www);

  if ((app == Qnil || app == Qfalse)) {
    fprintf(stderr,
            "* Iodine: (no app) the HTTP service on port %s will only serve "
            "static files.\n",
            (port ? StringValueCStr(port) : "3000"));
  }
  if (called_once)
    defer(iodine_print_http_msg2, (www ? (void *)www : NULL), (void *)port);
  else {
    called_once = 1;
    defer(iodine_print_http_msg1, (www ? (void *)www : NULL), (void *)port);
  }

  return Qtrue;
  (void)self;
}

static void initialize_env_template(void) {
  if (env_template_no_upgrade)
    return;
  env_template_no_upgrade = rb_hash_new();
  env_template_with_upgrade = rb_hash_new();

  rb_global_variable(&env_template_no_upgrade);
  rb_global_variable(&env_template_with_upgrade);

// Registry.add(env_template_no_upgrade);
// Registry.add(env_template_with_upgrade);

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

  // Start with the stuff Iodine will review.
  /* publish upgrade support */
  rb_hash_aset(env_template_with_upgrade, UPGRADE_WEBSOCKET_Q, Qtrue);
  rb_hash_aset(env_template_with_upgrade, UPGRADE_TCP_Q, Qtrue);
  rb_hash_aset(env_template_no_upgrade, UPGRADE_WEBSOCKET, Qnil);
  rb_hash_aset(env_template_no_upgrade, UPGRADE_TCP, Qnil);
  add_value_to_env(env_template_with_upgrade, "sendfile.type", XSENDFILE);
  add_value_to_env(env_template_with_upgrade, "HTTP_X_SENDFILE_TYPE",
                   XSENDFILE);
  add_value_to_env(env_template_no_upgrade, "sendfile.type", XSENDFILE);
  add_value_to_env(env_template_no_upgrade, "HTTP_X_SENDFILE_TYPE", XSENDFILE);
  // add the rack.version
  {
    static VALUE rack_version = 0;
    if (!rack_version) {
      rack_version = rb_ary_new(); // rb_ary_new is Ruby 2.0 compatible
      rb_ary_push(rack_version, INT2FIX(1));
      rb_ary_push(rack_version, INT2FIX(3));
      rb_global_variable(&rack_version);
      // Registry.add(rack_version);
    }
    add_value_to_env(env_template_with_upgrade, "rack.version", rack_version);
    add_value_to_env(env_template_no_upgrade, "rack.version", rack_version);
  }
  add_str_to_env(env_template_no_upgrade, "SCRIPT_NAME", "");
  add_str_to_env(env_template_with_upgrade, "SCRIPT_NAME", "");
  add_value_to_env(env_template_no_upgrade, "rack.errors", rb_stderr);
  add_value_to_env(env_template_no_upgrade, "rack.hijack?", Qtrue);
  add_value_to_env(env_template_no_upgrade, "rack.multiprocess", Qtrue);
  add_value_to_env(env_template_no_upgrade, "rack.multithread", Qtrue);
  add_value_to_env(env_template_no_upgrade, "rack.run_once", Qfalse);
  add_value_to_env(env_template_with_upgrade, "rack.errors", rb_stderr);
  add_value_to_env(env_template_with_upgrade, "rack.hijack?", Qtrue);
  add_value_to_env(env_template_with_upgrade, "rack.multiprocess", Qtrue);
  add_value_to_env(env_template_with_upgrade, "rack.multithread", Qtrue);
  add_value_to_env(env_template_with_upgrade, "rack.run_once", Qfalse);
  /* default schema to http, it might be updated later */
  rb_hash_aset(env_template_with_upgrade, R_URL_SCHEME, HTTP_SCHEME);
  rb_hash_aset(env_template_no_upgrade, R_URL_SCHEME, HTTP_SCHEME);
  /* placeholders... minimize rehashing*/
  rb_hash_aset(env_template_with_upgrade, REQUEST_METHOD, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, HTTP_VERSION, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, IODINE_R_HIJACK, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, PATH_INFO, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, QUERY_STRING, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, REMOTE_ADDR, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, REQUEST_METHOD, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, SERVER_NAME, QUERY_STRING);
  rb_hash_aset(env_template_no_upgrade, SERVER_PORT, QUERY_ESTRING);
  rb_hash_aset(env_template_no_upgrade, SERVER_PROTOCOL, QUERY_STRING);
  rb_hash_aset(env_template_with_upgrade, HTTP_VERSION, QUERY_STRING);
  rb_hash_aset(env_template_with_upgrade, IODINE_R_HIJACK, QUERY_STRING);
  rb_hash_aset(env_template_with_upgrade, PATH_INFO, QUERY_STRING);
  rb_hash_aset(env_template_with_upgrade, QUERY_STRING, QUERY_STRING);
  rb_hash_aset(env_template_with_upgrade, REMOTE_ADDR, QUERY_STRING);
  rb_hash_aset(env_template_with_upgrade, SERVER_NAME, QUERY_STRING);
  rb_hash_aset(env_template_with_upgrade, SERVER_PORT, QUERY_ESTRING);
  rb_hash_aset(env_template_with_upgrade, SERVER_PROTOCOL, QUERY_STRING);
}
/* *****************************************************************************
Initialization
***************************************************************************** */

void Iodine_init_http(void) {

  rb_define_module_function(Iodine, "listen2http", iodine_http_listen, 1);

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
  rack_set(HTTP_SCHEME, "http");
  rack_set(HTTPS_SCHEME, "https");
  rack_set(QUERY_ESTRING, "");
  rack_set(R_URL_SCHEME, "rack.url_scheme");
  rack_set(R_INPUT, "rack.input");
  rack_set(XSENDFILE, "X-Sendfile");
  rack_set(CONTENT_LENGTH_HEADER, "Content-Length");

  rack_set(IODINE_R_HIJACK_IO, "rack.hijack_io");
  rack_set(IODINE_R_HIJACK, "rack.hijack");
  rack_set(IODINE_R_HIJACK_CB, "iodine.hijack_cb");

  rack_set(UPGRADE_TCP, "upgrade.tcp");
  rack_set(UPGRADE_WEBSOCKET, "upgrade.websocket");

  rack_set(UPGRADE_TCP_Q, "upgrade.tcp?");
  rack_set(UPGRADE_WEBSOCKET_Q, "upgrade.websocket?");

  hijack_func_sym = ID2SYM(rb_intern("_hijack"));
  close_method_id = rb_intern("close");
  each_method_id = rb_intern("each");
  attach_method_id = rb_intern("attach_fd");

  IodineRackIO.init();
  initialize_env_template();
  Iodine_init_json();
}
