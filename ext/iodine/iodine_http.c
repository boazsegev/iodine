/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_http.h"
#include "iodine_websockets.h"
#include "rb-rack-io.h"

#include <arpa/inet.h>
#include <sys/socket.h>

/* *****************************************************************************
Available Globals
***************************************************************************** */
VALUE IodineHTTP;

typedef struct {
  VALUE app;
  VALUE env;
  unsigned long max_msg : 56;
  unsigned ping : 8;
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
// rack_declare(IODINE_R_HIJACK); // rack.hijack
// rack_declare(IODINE_R_HIJACK_CB);// rack.hijack_io

/* *****************************************************************************
Copying data from the C request to the Rack's ENV
***************************************************************************** */

#define to_upper(c) (((c) >= 'a' && (c) <= 'z') ? ((c) & ~32) : (c))

static inline VALUE copy2env(http_request_s *request, VALUE template) {
  VALUE env = rb_hash_dup(template);
  Registry.add(env);
  VALUE hname; /* will be used later, both as tmp and to iterate header names */
  char *pos = NULL;
  const char *reader = NULL;
  /* Copy basic data */
  rb_hash_aset(env, REQUEST_METHOD,
               rb_enc_str_new(request->method, request->method_len,
                              IodineBinaryEncoding));

  rb_hash_aset(
      env, PATH_INFO,
      rb_enc_str_new(request->path, request->path_len, IodineBinaryEncoding));
  rb_hash_aset(env, QUERY_STRING,
               (request->query
                    ? rb_enc_str_new(request->query, request->query_len,
                                     IodineBinaryEncoding)
                    : QUERY_ESTRING));
  rb_hash_aset(env, QUERY_STRING,
               (request->query
                    ? rb_enc_str_new(request->query, request->query_len,
                                     IodineBinaryEncoding)
                    : QUERY_ESTRING));

  hname = rb_enc_str_new(request->version, request->version_len,
                         IodineBinaryEncoding);
  rb_hash_aset(env, SERVER_PROTOCOL, hname);
  rb_hash_aset(env, HTTP_VERSION, hname);

  // Suppoer for Ruby web-console.
  hname = rb_str_buf_new(64);
  sock_peer_addr_s addrinfo = sock_peer_addr(request->fd);
  if (addrinfo.addrlen &&
      inet_ntop(
          addrinfo.addr->sa_family,
          addrinfo.addr->sa_family == AF_INET
              ? (void *)&((struct sockaddr_in *)addrinfo.addr)->sin_addr
              : (void *)&((struct sockaddr_in6 *)addrinfo.addr)->sin6_addr,
          RSTRING_PTR(hname), 64)) {
    rb_str_set_len(hname, strlen(RSTRING_PTR(hname)));
    rb_hash_aset(env, REMOTE_ADDR, hname);
  }

  /* setup input IO + hijack support */
  rb_hash_aset(env, R_INPUT, (hname = RackIO.create(request, env)));

  /* publish upgrade support */
  if (request->upgrade) {
    rb_hash_aset(env, UPGRADE_WEBSOCKET_Q, Qtrue);
    rb_hash_aset(env, UPGRADE_TCP_Q, Qtrue);
  }

  hname = rb_obj_method(hname, hijack_func_sym);
  rb_hash_aset(env, IODINE_R_HIJACK, hname);

  /* handle the HOST header, including the possible host:#### format*/
  pos = (char *)request->host;
  while (*pos && *pos != ':')
    pos++;
  if (*pos == 0) {
    rb_hash_aset(
        env, SERVER_NAME,
        rb_enc_str_new(request->host, request->host_len, IodineBinaryEncoding));
    rb_hash_aset(env, SERVER_PORT, QUERY_ESTRING);
  } else {
    rb_hash_aset(env, SERVER_NAME,
                 rb_enc_str_new(request->host, pos - request->host,
                                IodineBinaryEncoding));
    rb_hash_aset(env, SERVER_PORT,
                 rb_enc_str_new(pos + 1,
                                request->host_len - ((pos + 1) - request->host),
                                IodineBinaryEncoding));
  }

  /* default schema to http, it might be updated later */
  rb_hash_aset(env, R_URL_SCHEME, HTTP_SCHEME);

  /* add all headers, exclude special cases */
  http_header_s header = http_request_header_first(request);
  while (header.name) {
    if (header.name_len == 14 &&
        strncasecmp("content-length", header.name, 14) == 0) {
      rb_hash_aset(
          env, CONTENT_LENGTH,
          rb_enc_str_new(header.value, header.value_len, IodineBinaryEncoding));
      header = http_request_header_next(request);
      continue;
    } else if (header.name_len == 12 &&
               strncasecmp("content-type", header.name, 12) == 0) {
      rb_hash_aset(
          env, CONTENT_TYPE,
          rb_enc_str_new(header.value, header.value_len, IodineBinaryEncoding));
      header = http_request_header_next(request);
      continue;
    } else if (header.name_len == 27 &&
               strncasecmp("x-forwarded-proto", header.name, 27) == 0) {
      if (header.value_len >= 5 && !strncasecmp(header.value, "https", 5)) {
        rb_hash_aset(env, R_URL_SCHEME, HTTPS_SCHEME);
      } else if (header.value_len == 4 &&
                 *((uint32_t *)header.value) == *((uint32_t *)"http")) {
        rb_hash_aset(env, R_URL_SCHEME, HTTP_SCHEME);
      } else {
        rb_hash_aset(env, R_URL_SCHEME,
                     rb_enc_str_new(header.value, header.value_len,
                                    IodineBinaryEncoding));
      }
    } else if (header.name_len == 9 &&
               strncasecmp("forwarded", header.name, 9) == 0) {
      pos = (char *)header.value;
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
    }

    hname = rb_str_buf_new(6 + header.name_len);
    memcpy(RSTRING_PTR(hname), "HTTP_", 5);
    pos = RSTRING_PTR(hname) + 5;
    reader = header.name;
    while (*reader) {
      *(pos++) = *reader == '-' ? '_' : to_upper(*reader);
      ++reader;
    }
    *pos = 0;
    rb_str_set_len(hname, 5 + header.name_len);
    rb_hash_aset(
        env, hname,
        rb_enc_str_new(header.value, header.value_len, IodineBinaryEncoding));
    header = http_request_header_next(request);
  }
  return env;
}

/* *****************************************************************************
Handling the HTTP response
***************************************************************************** */

// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static int for_each_header_data(VALUE key, VALUE val, VALUE _res) {
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
  char *val_s = RSTRING_PTR(val);
  int val_len = RSTRING_LEN(val);
  // scan the value for newline (\n) delimiters
  int pos_s = 0, pos_e = 0;
  while (pos_e < val_len) {
    // scanning for newline (\n) delimiters
    while (pos_e < val_len && val_s[pos_e] != '\n')
      pos_e++;
    http_response_write_header(
        (void *)_res, .name = RSTRING_PTR(key), .name_len = RSTRING_LEN(key),
        .value = val_s + pos_s, .value_len = pos_e - pos_s);
    // fprintf(stderr, "For_each - headers: wrote header\n");
    // move forward (skip the '\n' if exists)
    pos_s = pos_e + 1;
    pos_e++;
  }
  // no errors, return 0
  return ST_CONTINUE;
}

// writes the body to the response object
static VALUE for_each_body_string(VALUE str, VALUE _res, int argc, VALUE argv) {
  (void)(argv);
  (void)(argc);
  // fprintf(stderr, "For_each - body\n");
  // write body
  if (TYPE(str) != T_STRING) {
    fprintf(stderr, "Iodine Server Error:"
                    "response body was not a String\n");
    return Qfalse;
  }
  if (RSTRING_LEN(str)) {
    if (http_response_write_body((void *)_res, RSTRING_PTR(str),
                                 RSTRING_LEN(str))) {
      // fprintf(stderr, "Iodine Server Error:"
      //                 "couldn't write response to connection\n");
      return Qfalse;
    }
  }
  return Qtrue;
}

static inline int ruby2c_response_send(http_response_s *response,
                                       VALUE rbresponse, VALUE env) {
  (void)(env);
  VALUE body = rb_ary_entry(rbresponse, 2);
  if (response->status < 200 || response->status == 204 ||
      response->status == 304) {
    if (rb_respond_to(body, close_method_id))
      RubyCaller.call(body, close_method_id);
    body = Qnil;
    response->content_length = -1;
  }

  if (TYPE(body) == T_ARRAY && RARRAY_LEN(body) == 1) { // [String] is likely
    body = rb_ary_entry(body, 0);
    // fprintf(stderr, "Body was a single item array, unpacket to string\n");
  }

  if (TYPE(body) == T_STRING) {
    // fprintf(stderr, "Review body as String\n");
    if (RSTRING_LEN(body))
      http_response_write_body(response, RSTRING_PTR(body), RSTRING_LEN(body));
    return 0;
  } else if (body == Qnil) {
    return 0;
  } else if (rb_respond_to(body, each_method_id)) {
    // fprintf(stderr, "Review body as for-each ...\n");
    if (!response->connection_written && !response->content_length_written) {
      // close the connection to indicate message length...
      // protection from bad code
      response->should_close = 1;
      response->content_length = -1;
    }
    rb_block_call(body, each_method_id, 0, NULL, for_each_body_string,
                  (VALUE)response);
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

static inline int ruby2c_review_upgrade(http_response_s *response,
                                        VALUE rbresponse, VALUE env) {
  VALUE handler;
  if ((handler = rb_hash_aref(env, IODINE_R_HIJACK_CB)) != Qnil) {
    // send headers
    http_response_finish(response);
    //  remove socket from libsock and libserver
    facil_attach(response->fd, NULL);
    // call the callback
    VALUE io_ruby = RubyCaller.call(rb_hash_aref(env, IODINE_R_HIJACK),
                                    iodine_call_proc_id);
    RubyCaller.call2(handler, iodine_call_proc_id, 1, &io_ruby);
  } else if ((handler = rb_hash_aref(env, IODINE_R_HIJACK_IO)) != Qnil) {
    // send nothing.
    http_response_destroy(response);
    // remove socket from libsock and libserver
    facil_attach(response->fd, NULL);
  } else if ((handler = rb_hash_aref(env, UPGRADE_WEBSOCKET)) != Qnil) {
    iodine_http_settings_s *settings = response->request->settings->udata;
    // use response as existing base for native websocket upgrade
    iodine_websocket_upgrade(response->request, response, handler,
                             settings->max_msg, settings->ping);
  } else if ((handler = rb_hash_aref(env, UPGRADE_TCP)) != Qnil) {
    intptr_t fduuid = response->fd;
    // send headers
    http_response_finish(response);
    // upgrade protocol
    VALUE args[2] = {(ULONG2NUM(sock_uuid2fd(fduuid))), handler};
    RubyCaller.call2(Iodine, attach_method_id, 2, args);
    // prevent response processing.
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

static void *on_rack_request_in_GVL(http_request_s *request) {
  http_response_s *response = http_response_create(request);
  iodine_http_settings_s *settings = request->settings->udata;
  if (request->settings->log_static)
    http_response_log_start(response);
  // create /register env variable
  VALUE env = copy2env(request, settings->env);
  // will be used later
  VALUE tmp;
  // pass env variable to handler
  VALUE rbresponse =
      RubyCaller.call2(settings->app, iodine_call_proc_id, 1, &env);
  if (rbresponse == 0 || rbresponse == Qnil)
    goto internal_error;
  Registry.add(rbresponse);
  // set response status
  tmp = rb_ary_entry(rbresponse, 0);
  if (TYPE(tmp) == T_STRING)
    tmp = rb_funcall2(tmp, to_fixnum_func_id, 0, NULL);
  if (TYPE(tmp) != T_FIXNUM)
    goto internal_error;
  response->status = FIX2ULONG(tmp);
  // handle header copy from ruby land to C land.
  VALUE response_headers = rb_ary_entry(rbresponse, 1);
  if (TYPE(response_headers) != T_HASH)
    goto internal_error;
  // extract the X-Sendfile header (never show original path)
  // X-Sendfile support only present when iodine sercers static files.
  VALUE xfiles = request->settings->public_folder
                     ? rb_hash_delete(response_headers, XSENDFILE)
                     : Qnil;
  // remove XFile's content length headers, as this will be controled by Iodine
  if (xfiles != Qnil) {
    rb_hash_delete(response_headers, CONTENT_LENGTH_HEADER);
  }
  // review each header and write it to the response.
  rb_hash_foreach(response_headers, for_each_header_data, (VALUE)(response));
  // If the X-Sendfile header was provided, send the file directly and finish
  if (xfiles != Qnil &&
      http_response_sendfile2(response, request, RSTRING_PTR(xfiles),
                              RSTRING_LEN(xfiles), NULL, 0, 1) == 0)
    goto external_done;
  // review for belated (post response headers) upgrade.
  if (ruby2c_review_upgrade(response, rbresponse, env))
    goto external_done;
  // send the request body.
  if (ruby2c_response_send(response, rbresponse, env))
    goto internal_error;

  Registry.remove(rbresponse);
  Registry.remove(env);
  http_response_finish(response);
  return NULL;
external_done:
  Registry.remove(rbresponse);
  Registry.remove(env);
  return NULL;
internal_error:
  Registry.remove(rbresponse);
  Registry.remove(env);
  http_response_destroy(response);
  response = http_response_create(request);
  if (request->settings->log_static)
    http_response_log_start(response);
  response->status = 500;
  http_response_write_body(response, "Error 500, Internal error.", 26);
  http_response_finish(response);
  return NULL;
}

static void on_rack_request(http_request_s *request) {
  // if (request->body_file)
  //   fprintf(stderr, "Request data is stored in a temporary file\n");
  RubyCaller.call_c((void *(*)(void *))on_rack_request_in_GVL, request);
}

/* *****************************************************************************
Initializing basic Rack ENV template
***************************************************************************** */

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

static void init_env_template(iodine_http_settings_s *set, uint8_t xsendfile) {
  VALUE tmp;
  set->env = rb_hash_new();
  Registry.add(set->env);

  // Start with the stuff Iodine will review.
  rb_hash_aset(set->env, UPGRADE_WEBSOCKET, Qnil);
  rb_hash_aset(set->env, UPGRADE_TCP, Qnil);
  if (xsendfile) {
    add_value_to_env(set->env, "sendfile.type", XSENDFILE);
    add_value_to_env(set->env, "HTTP_X_SENDFILE_TYPE", XSENDFILE);
  }
  rb_hash_aset(set->env, UPGRADE_WEBSOCKET_Q, Qnil);
  rb_hash_aset(set->env, UPGRADE_TCP_Q, Qnil);

  // add the rack.version
  tmp = rb_ary_new(); // rb_ary_new is Ruby 2.0 compatible
  rb_ary_push(tmp, INT2FIX(1));
  rb_ary_push(tmp, INT2FIX(3));
  // rb_ary_push(tmp, rb_enc_str_new("1", 1, IodineBinaryEncoding));
  // rb_ary_push(tmp, rb_enc_str_new("3", 1, IodineBinaryEncoding));
  add_value_to_env(set->env, "rack.version", tmp);
  add_value_to_env(set->env, "rack.errors", rb_stderr);
  add_value_to_env(set->env, "rack.multithread", Qtrue);
  add_value_to_env(set->env, "rack.multiprocess", Qtrue);
  add_value_to_env(set->env, "rack.run_once", Qfalse);
  add_value_to_env(set->env, "rack.hijack?", Qtrue);
  add_str_to_env(set->env, "SCRIPT_NAME", "");
}
#undef add_str_to_env
#undef add_value_to_env

/* *****************************************************************************
Listenninng to HTTP
***************************************************************************** */

void *iodine_print_http_msg2_in_gvl(void *d_) {
  // Write message
  struct {
    VALUE www;
    VALUE port;
  } *arg = d_;
  if (arg->www) {
    fprintf(stderr,
            " * Iodine HTTP Server on port %s:\n"
            " * Serving static files from %s\n",
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
            " * Serving static files from %s\n",
            StringValueCStr(arg->port), StringValueCStr(ruby_version),
            StringValueCStr(iodine_version), (size_t)sock_max_capacity(),
            StringValueCStr(arg->www));
    Registry.remove(arg->www);
  } else
    fprintf(stderr,
            "\nStarting up Iodine HTTP Server on port %s:\n"
            " * Ruby v.%s\n * Iodine v.%s \n"
            " * %lu max concurrent connections / open files\n",
            StringValueCStr(arg->port), StringValueCStr(ruby_version),
            StringValueCStr(iodine_version), (size_t)sock_max_capacity());
  Registry.remove(arg->port);

  return NULL;
}

static void iodine_print_http_msg1(void *www, void *port) {
  if (defer_fork_pid())
    return;
  struct {
    void *www;
    void *port;
  } data = {.www = www, .port = port};
  RubyCaller.call_c(iodine_print_http_msg_in_gvl, (void *)&data);
}
static void iodine_print_http_msg2(void *www, void *port) {
  if (defer_fork_pid())
    return;
  struct {
    void *www;
    void *port;
  } data = {.www = www, .port = port};
  RubyCaller.call_c(iodine_print_http_msg2_in_gvl, (void *)&data);
}

static void free_iodine_http(void *set_) {
  iodine_http_settings_s *set = set_;
  Registry.remove(set->app);
  Registry.remove(set->env);
  free(set);
}
/**
Listens to incoming HTTP connections and handles incoming requests using the
Rack specification.

This is delegated to a lower level C HTTP and Websocket implementation, no Ruby
object will be crated except the `env` object required by the Rack
specifications.

Accepts a single Hash argument with the following properties:

`app`:: the Rack application that handles incoming requests. Default: `nil`.
`port`:: the port to listen to. Default: 3000.
`address`:: the address to bind to. Default: binds to all possible addresses.
`log`:: enable response logging (Hijacked sockets aren't logged). Default: off.
`public`:: The root public folder for static file service. Default: none.
`timeout`:: Timeout for inactive HTTP/1.x connections. Defaults: 5 seconds.
`max_body`:: The maximum body size for incoming HTTP messages. Default: ~50Mib.
`max_msg`:: The maximum Websocket message size allowed. Default: ~250Kib.
`ping`:: The Websocket `ping` interval. Default: 40 sec.

Either the `app` or the `public` properties are required. If niether exists, the
function will fail. If both exist, Iodine will serve static files as well as
dynamic requests.

When using the static file server, it's possible to serve `gzip` versions of the
static files by saving a compressed version with the `gz` extension (i.e.
`styles.css.gz`).

`gzip` will only be served to clients tat support the `gzip` transfer encoding.

Once HTTP/2 is supported (planned, but probably very far away), HTTP/2 timeouts
will be dynamically managed by Iodine. The `timeout` option is only relevant to
HTTP/1.x connections.
*/
VALUE iodine_http_listen(VALUE self, VALUE opt) {
  static int called_once = 0;
  uint8_t log_http = 0;
  size_t ping = 0;
  size_t max_body = 0;
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
  } else
    port = rb_str_new("3000", 4);
  Registry.add(port);

  if ((app != Qnil && app != Qfalse))
    Registry.add(app);
  else
    app = 0;

  iodine_http_settings_s *set = malloc(sizeof(*set));
  *set = (iodine_http_settings_s){.app = app, .ping = ping, .max_msg = max_msg};

  init_env_template(set, (www ? 1 : 0));

  if (http_listen(StringValueCStr(port),
                  (address ? StringValueCStr(address) : NULL),
                  .on_request = on_rack_request, .udata = set,
                  .timeout = (tout ? FIX2INT(tout) : tout),
                  .on_finish = free_iodine_http, .log_static = log_http,
                  .max_body_size = max_body,
                  .public_folder = (www ? StringValueCStr(www) : NULL))) {
    fprintf(stderr,
            "ERROR: Failed to initialize a listening HTTP socket for port %s\n",
            port ? StringValueCStr(port) : "3000");
    return Qfalse;
  }

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

/* *****************************************************************************
Initialization
*****************************************************************************
*/

void Iodine_init_http(void) {

  IodineHTTP = rb_define_module_under(Iodine, "HTTP");

  rb_define_module_function(IodineHTTP, "listen", iodine_http_listen, 1);

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

  rack_set(QUERY_ESTRING, "");
  rack_set(QUERY_ESTRING, "");

  hijack_func_sym = ID2SYM(rb_intern("_hijack"));
  close_method_id = rb_intern("close");
  each_method_id = rb_intern("each");
  attach_method_id = rb_intern("attach_fd");

  RackIO.init();
}
