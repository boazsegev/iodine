/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_http.h"
#include "iodine_websocket.h"
#include "websockets.h"

/* the Iodine::Rack HTTP server class*/
VALUE IodineHttp;
/* these three are used also by rb-rack-io.c */
VALUE R_HIJACK;
VALUE R_HIJACK_IO;
VALUE R_HIJACK_CB;

VALUE UPGRADE_TCP;
VALUE UPGRADE_TCP_Q;
VALUE UPGRADE_WEBSOCKET;
VALUE UPGRADE_WEBSOCKET_Q;
/* backwards compatibility, temp */
VALUE IODINE_UPGRADE;
VALUE IODINE_WEBSOCKET;

static VALUE hijack_func_sym;
static ID to_fixnum_func_id;
static ID close_method_id;
static ID each_method_id;
static _Bool iodine_http_request_logging = 0;
static _Bool iodine_http_static_file_server = 0;
#define rack_declare(rack_name) static VALUE rack_name

#define rack_set(rack_name, str)                                               \
  (rack_name) = rb_enc_str_new((str), strlen((str)), BinaryEncoding);          \
  rb_global_variable(&(rack_name));                                            \
  rb_obj_freeze(rack_name);

#define rack_autoset(rack_name) rack_set((rack_name), #rack_name)

static VALUE ENV_TEMPLATE;

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
// rack_declare(R_HIJACK); // rack.hijack
// rack_declare(R_HIJACK_CB);// rack.hijack_io

/* *****************************************************************************
HTTP Protocol initialization
*/
/* allow quick access to the Rack app */
static VALUE rack_app_handler = 0;

#define to_upper(c) (((c) >= 'a' && (c) <= 'z') ? ((c) & ~32) : (c))

static inline VALUE copy2env(http_request_s *request) {
  VALUE env = rb_hash_dup(ENV_TEMPLATE);
  VALUE hname; /* will be used later, both as tmp and to iterate header names */
  char *pos = NULL;
  const char *reader = NULL;
  Registry.add(env);
  /* Copy basic data */
  rb_hash_aset(
      env, REQUEST_METHOD,
      rb_enc_str_new(request->method, request->method_len, BinaryEncoding));

  rb_hash_aset(
      env, PATH_INFO,
      rb_enc_str_new(request->path, request->path_len, BinaryEncoding));
  rb_hash_aset(
      env, QUERY_STRING,
      (request->query
           ? rb_enc_str_new(request->query, request->query_len, BinaryEncoding)
           : QUERY_ESTRING));
  rb_hash_aset(
      env, QUERY_STRING,
      (request->query
           ? rb_enc_str_new(request->query, request->query_len, BinaryEncoding)
           : QUERY_ESTRING));

  hname =
      rb_enc_str_new(request->version, request->version_len, BinaryEncoding);
  rb_hash_aset(env, SERVER_PROTOCOL, hname);
  rb_hash_aset(env, HTTP_VERSION, hname);

  // rack_declare(REMOTE_ADDR);
  // rb_hash_aset(env, REMOTE_ADDR, hname);

  /* setup input IO + hijack support */
  rb_hash_aset(env, R_INPUT, (hname = RackIO.create(request, env)));

  /* publish upgrade support */
  if (request->upgrade) {
    rb_hash_aset(env, UPGRADE_WEBSOCKET_Q, Qtrue);
    rb_hash_aset(env, UPGRADE_TCP_Q, Qtrue);
  }

  hname = rb_obj_method(hname, hijack_func_sym);
  rb_hash_aset(env, R_HIJACK, hname);

  /* handle the HOST header, including the possible host:#### format*/
  pos = (char *)request->host;
  while (*pos && *pos != ':')
    pos++;
  if (*pos == 0) {
    rb_hash_aset(
        env, SERVER_NAME,
        rb_enc_str_new(request->host, request->host_len, BinaryEncoding));
    rb_hash_aset(env, SERVER_PORT, QUERY_ESTRING);
  } else {
    rb_hash_aset(
        env, SERVER_NAME,
        rb_enc_str_new(request->host, pos - request->host, BinaryEncoding));
    rb_hash_aset(env, SERVER_PORT,
                 rb_enc_str_new(pos + 1,
                                request->host_len - ((pos + 1) - request->host),
                                BinaryEncoding));
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
          rb_enc_str_new(header.value, header.value_len, BinaryEncoding));
      header = http_request_header_next(request);
      continue;
    } else if (header.name_len == 12 &&
               strncasecmp("content-type", header.name, 12) == 0) {
      rb_hash_aset(
          env, CONTENT_TYPE,
          rb_enc_str_new(header.value, header.value_len, BinaryEncoding));
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
        rb_hash_aset(
            env, R_URL_SCHEME,
            rb_enc_str_new(header.value, header.value_len, BinaryEncoding));
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
        rb_enc_str_new(header.value, header.value_len, BinaryEncoding));
    header = http_request_header_next(request);
  }
  return env;
}

// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static int for_each_header_data(VALUE key, VALUE val, VALUE _res) {
  // fprintf(stderr, "For_each - headers\n");
  if (TYPE(key) != T_STRING)
    key = RubyCaller.call(key, to_s_method_id);
  if (TYPE(key) != T_STRING)
    return ST_CONTINUE;
  if (TYPE(val) != T_STRING) {
    val = RubyCaller.call(val, to_s_method_id);
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
      // fprintf(stderr,
      //         "Iodine Server Error:"
      //         "couldn't write response to connection\n");
      return Qfalse;
    }
  } else {
    return Qfalse;
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

static inline int ruby2c_review_upgrade(http_response_s *response,
                                        VALUE rbresponse, VALUE env) {
  VALUE handler;
  if ((handler = rb_hash_aref(env, R_HIJACK_CB)) != Qnil) {
    // send headers
    http_response_finish(response);
    //  remove socket from libsock and libserver
    facil_attach(response->fd, NULL);
    // call the callback
    VALUE io_ruby = RubyCaller.call(rb_hash_aref(env, R_HIJACK), call_proc_id);
    RubyCaller.call2(handler, call_proc_id, 1, &io_ruby);
  } else if ((handler = rb_hash_aref(env, R_HIJACK_IO)) != Qnil) {
    // send nothing.
    http_response_destroy(response);
    // remove socket from libsock and libserver
    facil_attach(response->fd, NULL);
  } else if ((handler = rb_hash_aref(env, UPGRADE_WEBSOCKET)) != Qnil ||
             (handler = rb_hash_aref(env, IODINE_WEBSOCKET)) != Qnil) {
    // use response as existing base for native websocket upgrade
    iodine_websocket_upgrade(response->request, response, handler);
  } else if ((handler = rb_hash_aref(env, UPGRADE_TCP)) != Qnil ||
             (handler = rb_hash_aref(env, IODINE_UPGRADE)) != Qnil) {
    intptr_t fduuid = response->fd;
    // send headers
    http_response_finish(response);
    // upgrade protocol
    iodine_upgrade2basic(fduuid, handler);
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

static void *on_rack_request_in_GVL(http_request_s *request) {
  http_response_s *response = http_response_create(request);
  if (iodine_http_request_logging)
    http_response_log_start(response);
  // create /register env variable
  VALUE env = copy2env(request);
  // will be used later
  VALUE tmp;
  // pass env variable to handler
  VALUE rbresponse = RubyCaller.call2(rack_app_handler, call_proc_id, 1, &env);
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
  VALUE xfiles = iodine_http_static_file_server
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
  if (iodine_http_request_logging)
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
Initializing basic ENV template
*/

#define add_str_to_env(env, key, value)                                        \
  {                                                                            \
    VALUE k = rb_enc_str_new((key), strlen((key)), BinaryEncoding);            \
    rb_obj_freeze(k);                                                          \
    VALUE v = rb_enc_str_new((value), strlen((value)), BinaryEncoding);        \
    rb_obj_freeze(v);                                                          \
    rb_hash_aset(env, k, v);                                                   \
  }
#define add_value_to_env(env, key, value)                                      \
  {                                                                            \
    VALUE k = rb_enc_str_new((key), strlen((key)), BinaryEncoding);            \
    rb_obj_freeze(k);                                                          \
    rb_hash_aset(env, k, value);                                               \
  }

static void init_env_template(void) {
  VALUE tmp;
  ENV_TEMPLATE = rb_hash_new();
  rb_global_variable(&ENV_TEMPLATE);

  // Start with the stuff Iodine will review.
  rb_hash_aset(ENV_TEMPLATE, UPGRADE_WEBSOCKET, Qnil);
  rb_hash_aset(ENV_TEMPLATE, UPGRADE_TCP, Qnil);
  if (iodine_http_static_file_server) {
    add_value_to_env(ENV_TEMPLATE, "sendfile.type", XSENDFILE);
    add_value_to_env(ENV_TEMPLATE, "HTTP_X_SENDFILE_TYPE", XSENDFILE);
  }
  rb_hash_aset(ENV_TEMPLATE, UPGRADE_WEBSOCKET_Q, Qnil);
  rb_hash_aset(ENV_TEMPLATE, UPGRADE_TCP_Q, Qnil);

  /* backwards compatibility, temp */
  rb_hash_aset(ENV_TEMPLATE, IODINE_WEBSOCKET, Qnil);
  rb_hash_aset(ENV_TEMPLATE, IODINE_UPGRADE, Qnil);

  // add the rack.version
  tmp = rb_ary_new(); // rb_ary_new is Ruby 2.0 compatible
  rb_ary_push(tmp, INT2FIX(1));
  rb_ary_push(tmp, INT2FIX(3));
  // rb_ary_push(tmp, rb_enc_str_new("1", 1, BinaryEncoding));
  // rb_ary_push(tmp, rb_enc_str_new("3", 1, BinaryEncoding));
  add_value_to_env(ENV_TEMPLATE, "rack.version", tmp);
  add_value_to_env(ENV_TEMPLATE, "rack.errors", rb_stderr);
  add_value_to_env(ENV_TEMPLATE, "rack.multithread", Qtrue);
  add_value_to_env(ENV_TEMPLATE, "rack.multiprocess", Qtrue);
  add_value_to_env(ENV_TEMPLATE, "rack.run_once", Qfalse);
  add_value_to_env(ENV_TEMPLATE, "rack.hijack?", Qtrue);
  add_str_to_env(ENV_TEMPLATE, "SCRIPT_NAME", "");
}
#undef add_str_to_env
#undef add_value_to_env

/* *****************************************************************************
Rack object API
*/

int iodine_http_review(void) {
  rack_app_handler = rb_iv_get(IodineHttp, "@app");
  if (rack_app_handler != Qnil &&
      rb_respond_to(rack_app_handler, call_proc_id)) {
    VALUE rbport = rb_iv_get(IodineHttp, "@port");
    VALUE rbaddress = rb_iv_get(IodineHttp, "@address");
    VALUE rbmaxbody = rb_iv_get(IodineHttp, "@max_body_size");
    VALUE rbmaxmsg = rb_iv_get(IodineHttp, "@max_msg_size");
    VALUE rbwww = rb_iv_get(IodineHttp, "@public");
    VALUE rblog = rb_iv_get(IodineHttp, "@log");
    VALUE rbtout = rb_iv_get(IodineHttp, "@timeout");
    VALUE rbwstout = rb_iv_get(IodineHttp, "@ws_timeout");
    const char *port = "3000";
    const char *address = NULL;
    const char *public_folder = NULL;
    size_t max_body_size;
    // review port
    if (TYPE(rbport) != T_FIXNUM && TYPE(rbport) != T_STRING &&
        TYPE(rbport) != Qnil)
      rb_raise(rb_eTypeError,
               "The port variable must be either a Fixnum or a String.");
    if (TYPE(rbport) == T_FIXNUM) {
      rbport = rb_funcall2(rbport, rb_intern("to_s"), 0, NULL);
      // rb_ivar_set(self, rb_intern("_port"), port);
      rb_iv_set(IodineHttp, "@port", rbport);
    }
    if (TYPE(rbport) == T_STRING)
      port = StringValueCStr(rbport);
    // review address
    if (TYPE(rbaddress) != T_STRING && rbaddress != Qnil)
      rb_raise(rb_eTypeError,
               "The address variable must be either a String or `nil`.");
    if (TYPE(address) == T_STRING)
      address = StringValueCStr(rbaddress);
    // review public folder
    if (TYPE(rbwww) != T_STRING && rbwww != Qnil)
      rb_raise(rb_eTypeError,
               "The public folder variable `public` must be either a String or "
               "`nil`.");
    if (TYPE(rbwww) == T_STRING) {
      public_folder = StringValueCStr(rbwww);
      iodine_http_static_file_server = 1;
    }
    // review timeout
    uint8_t timeout = (TYPE(rbtout) == T_FIXNUM) ? FIX2ULONG(rbtout) : 0;
    if (FIX2ULONG(rbtout) > 255) {
      fprintf(stderr,
              "Iodine Warning: Iodine::Rack timeout value is over 255 and is "
              "silently ignored.\n");
      timeout = 0;
    }
    // review websocket timeout
    iodine_websocket_timeout =
        (TYPE(rbwstout) == T_FIXNUM) ? FIX2ULONG(rbwstout) : 0;
    if (FIX2ULONG(rbwstout) > 255) {
      fprintf(stderr, "Iodine Warning: Iodine::Rack Websocket timeout value "
                      "is over 255 and is silently ignored.\n");
      iodine_websocket_timeout = 0;
    }
    // review max body size
    max_body_size = (TYPE(rbmaxbody) == T_FIXNUM) ? FIX2ULONG(rbmaxbody) : 0;
    // review max websocket message size
    iodine_websocket_max_msg_size =
        (TYPE(rbmaxmsg) == T_FIXNUM) ? FIX2ULONG(rbmaxmsg) : 0;
    // review logging
    iodine_http_request_logging = (rblog != Qnil && rblog != Qfalse);

    // initialize the Rack env template
    init_env_template();

    // Write message
    VALUE iodine_version = rb_const_get(Iodine, rb_intern("VERSION"));
    VALUE ruby_version = rb_const_get(Iodine, rb_intern("RUBY_VERSION"));
    if (public_folder)
      fprintf(stderr,
              "Starting up Iodine HTTP Server:\n"
              " * Ruby v.%s\n * Iodine v.%s \n"
              " * %lu max concurrent connections / open files\n"
              " * Serving static files from:\n"
              "           %s\n\n",
              StringValueCStr(ruby_version), StringValueCStr(iodine_version),
              (size_t)sock_max_capacity(), public_folder);
    else
      fprintf(stderr,
              "Starting up Iodine HTTP Server:\n"
              " * Ruby v.%s\n * Iodine v.%s \n"
              " * %lu max concurrent connections / open files\n"
              "\n",
              StringValueCStr(ruby_version), StringValueCStr(iodine_version),
              (size_t)sock_max_capacity());

    // listen
    return http_listen(port, address, .on_request = on_rack_request,
                       .log_static = iodine_http_request_logging,
                       .max_body_size = max_body_size,
                       .public_folder = public_folder, .timeout = timeout);
  }
  return 0;
}

/* *****************************************************************************
Initializing the library
*/

void Init_iodine_http(void) {
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

  rack_set(R_HIJACK_IO, "rack.hijack_io");
  rack_set(R_HIJACK, "rack.hijack");
  rack_set(R_HIJACK_CB, "iodine.hijack_cb");

  rack_set(UPGRADE_TCP, "upgrade.tcp");
  rack_set(UPGRADE_WEBSOCKET, "upgrade.websocket");

  rack_set(UPGRADE_TCP_Q, "upgrade.tcp?");
  rack_set(UPGRADE_WEBSOCKET_Q, "upgrade.websocket?");

  /* backwards compatability, temp */
  rack_set(IODINE_UPGRADE, "iodine.upgrade");
  rack_set(IODINE_WEBSOCKET, "iodine.websocket");

  rack_set(QUERY_ESTRING, "");
  rack_set(QUERY_ESTRING, "");

  hijack_func_sym = ID2SYM(rb_intern("_hijack"));
  to_fixnum_func_id = rb_intern("to_i");
  close_method_id = rb_intern("close");
  each_method_id = rb_intern("each");

  IodineHttp = rb_define_module_under(Iodine, "Rack");
  RackIO.init();
  Init_iodine_websocket();
}

// REQUEST_METHOD
// PATH_INFO
// QUERY_STRING
// SERVER_NAME
// SERVER_PORT
// CONTENT_LENGTH
// rack.url_scheme rack.input rack.hijack rack.hijack_io HTTP_ Variables
