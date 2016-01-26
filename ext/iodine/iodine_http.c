#include "iodine_http.h"
#include <ruby/io.h>

/* ////////////////////////////////////////////////////////////
This file creates an HTTP server based on the Iodine libraries.

The server is (mostly) Rack compatible, except:

1. upgrade requests are handled using special upgrade handlers.
2. if a String is returned, it is a assumed to be a status 200 Html data?

//////////////////////////////////////////////////////////// */

//////////////
// general global definitions we will use herein.
static int BinaryEncodingIndex;      // encoding index
static rb_encoding* BinaryEncoding;  // encoding object
static VALUE rHttp;                  // The Iodine::Http class
static VALUE rIodine;                // The Iodine class
static VALUE rServer;                // server object to Ruby class
static ID server_var_id;             // id for the Server variable (pointer)
static ID fd_var_id;                 // id for the file descriptor (Fixnum)
static ID call_proc_id;              // id for `#call`
static ID each_method_id;            // id for `#call`
static ID to_s_method_id;            // id for `#call`
// for Rack
static VALUE REQUEST_METHOD;   // for Rack
static VALUE CONTENT_TYPE;     // for Rack.
static VALUE CONTENT_LENGTH;   // for Rack.
static VALUE SCRIPT_NAME;      // for Rack
static VALUE PATH_INFO;        // for Rack
static VALUE QUERY_STRING;     // for Rack
static VALUE QUERY_ESTRING;    // for rack (if no query)
static VALUE SERVER_NAME;      // for Rack
static VALUE SERVER_PORT;      // for Rack
static VALUE SERVER_PORT_80;   // for Rack
static VALUE SERVER_PORT_443;  // for Rack
static VALUE R_VERSION;        // for Rack: rack.version
static VALUE R_VERSION_V;      // for Rack: rack.version
static VALUE R_SCHEME;         // for Rack: rack.url_scheme
static VALUE R_SCHEME_HTTP;    // for Rack: rack.url_scheme value
static VALUE R_SCHEME_HTTPS;   // for Rack: rack.url_scheme value
static VALUE R_INPUT;          // for Rack: rack.input
static VALUE R_ERRORS;         // for Rack: rack.errors
static VALUE R_ERRORS_V;       // for Rack: rack.errors
static VALUE R_MTHREAD;        // for Rack: rack.multithread
static VALUE R_MTHREAD_V;      // for Rack: rack.multithread
static VALUE R_MPROCESS;       // for Rack: rack.multiprocess
static VALUE R_MPROCESS_V;     // for Rack: rack.multiprocess
static VALUE R_RUN_ONCE;       // for Rack: rack.run_once
static VALUE R_HIJACK_Q;       // for Rack: rack.hijack?
static VALUE R_HIJACK_Q_V;     // for Rack: rack.hijack?
static VALUE R_HIJACK;         // for Rack: rack.hijack
static VALUE R_HIJACK_V;       // for Rack: rack.hijack
static VALUE R_HIJACK_IO;      // for Rack: rack.hijack_io
static VALUE R_HIJACK_IO_V;    // for Rack: rack.hijack_io

// rack.version must be an array of Integers.
// rack.url_scheme must either be http or https.
// There must be a valid input stream in rack.input.
// There must be a valid error stream in rack.errors.
// There may be a valid hijack stream in rack.hijack_io
// The REQUEST_METHOD must be a valid token.
// The SCRIPT_NAME, if non-empty, must start with /
// The PATH_INFO, if non-empty, must start with /
// The CONTENT_LENGTH, if given, must consist of digits only.
// One of SCRIPT_NAME or PATH_INFO must be set. PATH_INFO should be / if
// SCRIPT_NAME is empty. SCRIPT_NAME never should be /, but instead be empty.

/* ////////////////////////////////////////////////////////////

The Http on_request handling functions

//////////////////////////////////////////////////////////// */

// translate a struct HttpRequest to a Hash, according top the
// Rack specifications.
static VALUE request_to_env(struct HttpRequest* request) {
  // return Qnil;
  // Create the env Hash
  VALUE env = rb_hash_new();
  // Register the object
  Registry.add(env);
  // set the simple core settings

  rb_hash_aset(
      env, REQUEST_METHOD,
      rb_enc_str_new(request->method, strlen(request->method), BinaryEncoding));
  rb_hash_aset(
      env, PATH_INFO,
      rb_enc_str_new(request->path, strlen(request->path), BinaryEncoding));
  rb_hash_aset(
      env, QUERY_STRING,
      (request->query ? rb_enc_str_new(request->query, strlen(request->query),
                                       BinaryEncoding)
                      : QUERY_ESTRING));
  // setup static env data
  rb_hash_aset(env, R_VERSION, R_VERSION_V);
  rb_hash_aset(env, SCRIPT_NAME, QUERY_ESTRING);
  rb_hash_aset(env, R_ERRORS, R_ERRORS_V);
  rb_hash_aset(env, R_MTHREAD, R_MTHREAD_V);
  rb_hash_aset(env, R_MPROCESS, R_MPROCESS_V);
  rb_hash_aset(env, R_HIJACK_Q, R_HIJACK_Q_V);
  rb_hash_aset(env, R_HIJACK, R_HIJACK_V);
  rb_hash_aset(env, R_HIJACK_Q, R_HIJACK_Q_V);
  rb_hash_aset(env, R_HIJACK_IO, R_HIJACK_IO_V);
  rb_hash_aset(env, R_RUN_ONCE, Qfalse);
  // set scheme to R_SCHEME_HTTP or R_SCHEME_HTTPS or dynamic
  int ssl = 0;
  {
    if (HttpRequest.find(request, "X-FORWARDED-PROTO")) {
      // this is the protocol
      char* proto = HttpRequest.value(request);
      if (!strcasecmp(proto, "http")) {
        rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
      } else if (!strcasecmp(proto, "https")) {
        ssl = 1;
        rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTPS);
      } else {
        rb_hash_aset(env, R_SCHEME, rb_str_new_cstr(proto));
      }
    } else if (HttpRequest.find(request, "FORWARDED")) {
      // placeholder
      char* proto = HttpRequest.value(request);
      int pos = 0;
      int len = strlen(proto);
      while (pos < len) {
        if (((proto[pos++] | 32) == 'p') && ((proto[pos++] | 32) == 'r') &&
            ((proto[pos++] | 32) == 'o') && ((proto[pos++] | 32) == 't') &&
            ((proto[pos++] | 32) == 'o') && ((proto[pos++] | 32) == '=')) {
          if (!strncasecmp(proto + pos, "http", 4)) {
            if ((proto + pos)[4] == 's') {
              ssl = 1;
              rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTPS);
            } else {
              rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
            }
          } else {
            int pos_e = pos;
            while ((pos_e < len) && (proto[pos_e] != ';'))
              pos_e++;
            rb_hash_aset(env, R_SCHEME, rb_str_new(proto + pos, pos_e - pos));
          }
        }
      }
      // default to http
      if (pos == len)
        rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
    } else
      rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
  }
  // set host data
  {
    int pos = 0;
    int len = strlen(request->host);
    for (; pos < len; pos++) {
      if (request->host[pos] == ':')
        break;
    }
    rb_hash_aset(env, SERVER_NAME, rb_str_new(request->host, pos));
    if (++pos < len)
      rb_hash_aset(env, SERVER_PORT,
                   rb_str_new(request->host + pos, len - pos));
    else
      rb_hash_aset(env, SERVER_PORT, (ssl ? SERVER_PORT_443 : SERVER_PORT_80));
  }
  // set POST data
  if (request->content_length) {
    // check for content type
    if (request->content_type) {
      rb_hash_aset(
          env, CONTENT_TYPE,
          rb_enc_str_new(request->content_type, strlen(request->content_type),
                         BinaryEncoding));
    }
    // CONTENT_LENGTH should be a string (damn stupid Rack specs).
    HttpRequest.find(request, "CONTENT-LENGTH");
    char* value = HttpRequest.value(request);
    rb_hash_aset(env, CONTENT_LENGTH,
                 rb_enc_str_new(value, strlen(value), BinaryEncoding));
  }
  rb_hash_aset(env, R_INPUT, RackIO.new(request));
  // itterate through the headers and set the HTTP_X "variables"
  HttpRequest.first(request);
  {
    char *name, *value;
    VALUE header;
    do {
      name = HttpRequest.name(request);
      value = HttpRequest.value(request);
      // careful, pointer comparison crashed Ruby (although it works without
      // Ruby)... this could be an issue.
      if (value == (request->content_type) ||
          (name[0] == 'C' && !strcmp(name, "CONTENT-LENGTH")))
        continue;
      header = rb_sprintf("HTTP_%s", name);
      rb_enc_associate(header, BinaryEncoding);
      rb_hash_aset(env, header,
                   rb_enc_str_new(value, strlen(value), BinaryEncoding));
    } while (HttpRequest.next(request));
  }
  return env;
}

// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static int for_each_header_pair(VALUE key, VALUE val, VALUE _req) {
  struct HttpRequest* request = (void*)_req;
  int pos_s = 0, pos_e = 0;
  if (TYPE(key) != T_STRING)
    key = RubyCaller.call_unsafe(key, to_s_method_id);
  if (TYPE(val) != T_STRING) {
    if (TYPE(val) == T_FIXNUM) {
      request->private.pos +=
          snprintf(request->buffer + request->private.pos,
                   HTTP_HEAD_MAX_SIZE - request->private.pos, "%.*s: %ld\r\n",
                   (int)RSTRING_LEN(key), RSTRING_PTR(key), FIX2LONG(val));
      return ST_CONTINUE;
    }
    val = RubyCaller.call_unsafe(val, to_s_method_id);
    if (val == Qnil)
      return ST_STOP;
  }
  char* key_s = RSTRING_PTR(key);
  char* val_s = RSTRING_PTR(val);
  int key_len = RSTRING_LEN(key), val_len = RSTRING_LEN(val);
  // scan the value for newline (\n) delimiters
  while (pos_e < val_len) {
    // make sure we don't overflow
    if (request->private.pos >= HTTP_HEAD_MAX_SIZE)
      return ST_STOP;
    // scanning for newline (\n) delimiters
    while (pos_e < val_len && val_s[pos_e] != '\n')
      pos_e++;
    // whether we hit a delimitor or the end of string, write the header
    request->private.pos +=
        snprintf(request->buffer + request->private.pos,
                 HTTP_HEAD_MAX_SIZE - request->private.pos, "%.*s: %.*s\r\n",
                 key_len, key_s, pos_e - pos_s, val_s + pos_s);
    // move forward (skip the '\n' if exists)
    pos_s = pos_e + 1;
    pos_e++;
  }
  // no errors, return 0
  return ST_CONTINUE;
}
// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static VALUE for_each_string(VALUE str, VALUE _req, int argc, VALUE argv) {
  struct HttpRequest* request = (void*)_req;
  // write body
  if (TYPE(str) != T_STRING) {
    return Qfalse;
  }
  Server.write(request->server, request->sockfd, RSTRING_PTR(str),
               RSTRING_LEN(str));
  return Qtrue;
}
// translate a struct HttpRequest to a Hash, according top the
// Rack specifications.
static void send_response(struct HttpRequest* request, VALUE response) {
  static char internal_error[] =
      "HTTP/1.1 502 Internal Error\r\n"
      "Connection: closed\r\n"
      "Content-Length: 16\r\n\r\n"
      "Internal Error\r\n";
  // nil is a bad response... we have an error
  if (response == Qnil)
    goto internal_err;
  if (TYPE(response) != T_ARRAY)
    goto internal_err;
  if (RARRAY_LEN(response) < 3)
    goto internal_err;

  VALUE tmp;
  char* tmp_s;
  char close_when_done = 0;

  // check for keep alive
  if (HttpRequest.find(request, "CONNECTION")) {
    if (HttpRequest.value(request)[0] == 'c')
      close_when_done = 1;
  } else if (request->version[6] != '.') {
    close_when_done = 1;
  }
  // get status code from array (obj 0)
  // NOTICE: this may not always be a number (could be the string "200").
  tmp = rb_ary_entry(response, 0);
  if (TYPE(tmp) == T_STRING)
    tmp = rb_str_to_inum(tmp, 10, 0);
  if (TYPE(tmp) != T_FIXNUM || (tmp = FIX2INT(tmp)) > 512 || tmp < 100 ||
      !(tmp_s = HttpStatus.to_s(tmp)))
    goto internal_err;
  request->private.pos = 0;
  request->private.pos +=
      snprintf(request->buffer, HTTP_HEAD_MAX_SIZE - request->private.pos,
               "HTTP/1.1 %lu %s\r\n", tmp, tmp_s);

  // Start printing headers to head-buffer
  tmp = rb_ary_entry(response, 1);
  if (TYPE(tmp) != T_HASH)
    goto internal_err;
  rb_hash_foreach(tmp, for_each_header_pair, (VALUE)request);
  // make sure we're not overflowing
  if (request->private.pos >= HTTP_HEAD_MAX_SIZE - 2) {
    rb_warn("Header overflow detected! Header size is limited to ~8Kb.");
    goto internal_err;
  }
  // review connection (+ keep alive) and date headers
  tmp = 0;
  request->private.max = 0;
  // review buffer and check if the headers exist
  while (request->private.max < request->private.pos) {
    if ((request->buffer[request->private.max++]) != '\n')
      continue;
    if ((request->buffer[request->private.max++] | 32) == 'c' &&
        (request->buffer[request->private.max++] | 32) == 'o' &&
        (request->buffer[request->private.max++] | 32) == 'n' &&
        (request->buffer[request->private.max++] | 32) == 'n' &&
        (request->buffer[request->private.max++] | 32) == 'e' &&
        (request->buffer[request->private.max++] | 32) == 'c' &&
        (request->buffer[request->private.max++] | 32) == 't' &&
        (request->buffer[request->private.max++] | 32) == 'i' &&
        (request->buffer[request->private.max++] | 32) == 'o' &&
        (request->buffer[request->private.max++] | 32) == 'n' &&
        (request->buffer[request->private.max++] | 32) == ':') {
      tmp = tmp | 2;
      // check for close twice, as the first 'c' could be a space
      if (request->buffer[request->private.max++] == 'c' ||
          request->buffer[request->private.max++] == 'c') {
        fprintf(stderr, "found a 'close' header\n");
        close_when_done = 1;
      }
    }
    if ((request->buffer[request->private.max++] | 32) == 'd' &&
        (request->buffer[request->private.max++] | 32) == 'a' &&
        (request->buffer[request->private.max++] | 32) == 't' &&
        (request->buffer[request->private.max++] | 32) == 'e' &&
        (request->buffer[request->private.max++] | 32) == ':')
      tmp = tmp | 4;
  }
  // if the connection headers aren't there, add them
  if (!(tmp & 2)) {
    if (close_when_done) {
      static char close_conn_header[] = "Connection: close\r\n";
      if (request->private.pos + sizeof(close_conn_header) >=
          HTTP_HEAD_MAX_SIZE - 2) {
        rb_warn("Header overflow detected! Header size is limited to ~8Kb.");
        goto internal_err;
      }
      memcpy(request->buffer + request->private.pos, close_conn_header,
             sizeof(close_conn_header));
      request->private.pos += sizeof(close_conn_header) - 1;
    } else {
      static char kpalv_conn_header[] =
          "Connection: keep-alive\r\nKeep-Alive: timeout=2\r\n";
      if (request->private.pos + sizeof(kpalv_conn_header) >=
          HTTP_HEAD_MAX_SIZE - 2) {
        rb_warn("Header overflow detected! Header size is limited to ~8Kb.");
        goto internal_err;
      }
      memcpy(request->buffer + request->private.pos, kpalv_conn_header,
             sizeof(kpalv_conn_header));
      request->private.pos += sizeof(kpalv_conn_header) - 1;
    }
  }
  if (!(tmp & 4)) {
    int ___todo_date;
  }
  // write the extra EOL markers
  request->buffer[request->private.pos++] = '\r';
  request->buffer[request->private.pos++] = '\n';

  // write headers to server
  Server.write(request->server, request->sockfd, request->buffer,
               request->private.pos);

  // write body
  tmp = rb_ary_entry(response, 2);
  if (TYPE(tmp) == T_ARRAY) {
    // [String] is most likely
    int len = RARRAY_LEN(tmp);
    VALUE str;
    for (size_t i = 0; i < len; i++) {
      str = rb_ary_entry(tmp, i);
      if (TYPE(str) != T_STRING) {
        fprintf(stderr, "data in array isn't a string! (index %lu)\n", i);
        goto internal_err;
      }
      Server.write(request->server, request->sockfd, RSTRING_PTR(str),
                   RSTRING_LEN(str));
    }
  } else if (TYPE(tmp) == T_STRING) {
    // String is a likely error
    Server.write(request->server, request->sockfd, RSTRING_PTR(tmp),
                 RSTRING_LEN(tmp));
  } else {
    rb_block_call(tmp, each_method_id, 0, NULL, for_each_string,
                  (VALUE)request);
  }

  if (close_when_done)
    Server.close(request->server, request->sockfd);

  // Upgrade (if 4th element)
  if (RARRAY_LEN(response) > 3) {
    int todo;
    tmp = rb_ary_entry(response, 3);
  }
  // Registry.remove(response);
  return;
internal_err:
  // Registry.remove(response);
  Server.write(request->server, request->sockfd, internal_error,
               sizeof(internal_error));
  Server.close(request->server, request->sockfd);
  rb_warn(
      "Invalid HTTP response, send 502 error code and closed connectiom.\n"
      "The response must be an Array:\n[<Fixnum - status code>, "
      "{<Hash-headers>}, [<Array/IO - data], <optional Protocol "
      "for Upgrade>]");
}

// Gets the response object, within a GVL context
static void* handle_request_in_gvl(void* _res) {
  struct HttpRequest* request = _res;
  VALUE env = request_to_env(request);
  // a regular request is forwarded to the on_request callback.
  VALUE response = RubyCaller.call_unsafe2(
      (VALUE)Server.get_udata(request->server, 0), call_proc_id, 1, &env);
  // clean-up env and register response
  if (Registry.replace(env, response))
    Registry.add(response);
  send_response(request, response);
  Registry.remove(response);
  return 0;
}
// The core handler passed on to the HttpProtocol object.
static void on_request(struct HttpRequest* request) {
  // work inside the GVL
  rb_thread_call_with_gvl(handle_request_in_gvl, request);
}

/* ////////////////////////////////////////////////////////////

The main class - Iodine::Http

//////////////////////////////////////////////////////////// */

/////////////////////////////
// server callbacks

// called when the server starts up. Saves the server object to the
// instance.
static void on_init(struct Server* server) {
  VALUE core_instance = ((VALUE)Server.settings(server)->udata);
  // save the updated on_request  as a global value on the server, using
  // fd=0
  Server.set_udata(server, 0, (void*)rb_iv_get(core_instance, "@on_request"));
  // message
  VALUE version_val = rb_const_get(rIodine, rb_intern("VERSION"));
  char* version_str = StringValueCStr(version_val);
  fprintf(stderr,
          "Starting up Iodine's HTTP server, V. %s using %d thread%s X %d "
          "processes\n",
          version_str, Server.settings(server)->threads,
          (Server.settings(server)->threads > 1 ? "s" : ""),
          Server.settings(server)->processes);
  // set the server variable in the core server object.. is this GC safe?
  set_server(core_instance, server);
  // perform on_init callback
  VALUE on_start_block = rb_iv_get(core_instance, "on_start_block");
  if (on_start_block != Qnil) {
    RubyCaller.call(on_start_block, call_proc_id);
  }
}

// called when server is idling
static void on_idle(struct Server* srv) {
  // call(reg->obj, on_data_func_id);
  // rb_gc_start();
}

/////////////////////////////
// Running the Http server
//
// the no-GVL state
static void* srv_start_no_gvl(void* _settings) {
  struct ServerSettings* settings = _settings;
  long ret = Server.listen(*settings);
  if (ret < 0)
    perror("Couldn't start server");
  return 0;
}
//
// The stop unblock fun
static void unblck(void* _) {
  Server.stop_all();
}
//
// This method starts the Http server instance.
static VALUE http_start(VALUE self) {
  // review the callback
  VALUE on_request_handler = rb_iv_get(self, "@on_request");
  if (rb_obj_method(on_request_handler, ID2SYM(call_proc_id)) == Qnil) {
    rb_raise(rb_eTypeError,
             "The on_request callback should be an object that answers to the "
             "method `call`");
    return Qnil;
  }
  // get current settings
  // load the settings from the Ruby layer to the C layer
  VALUE rb_port = rb_ivar_get(self, rb_intern("@port"));
  VALUE rb_bind = rb_ivar_get(self, rb_intern("@address"));
  VALUE rb_timeout = rb_ivar_get(self, rb_intern("@timeout"));
  VALUE rb_threads = rb_ivar_get(self, rb_intern("@threads"));
  VALUE rb_processes = rb_ivar_get(self, rb_intern("@processes"));
  // validate port
  if (rb_port != Qnil && TYPE(rb_port) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "port isn't a valid number.");
    return Qnil;
  }
  int iport = rb_port == Qnil ? 3000 : FIX2INT(rb_port);
  if (iport > 65535 || iport < 0) {
    rb_raise(rb_eTypeError, "port out of range.");
    return Qnil;
  }
  // validate bind
  if (rb_bind != Qnil && TYPE(rb_bind) != T_STRING) {
    rb_raise(rb_eTypeError, "bind should be either a String or nil.");
    return Qnil;
  }
  if (rb_bind != Qnil)
    rb_warn("the `bind` property is ignored, unimplemented yet");
  // validate timeout
  if (rb_timeout != Qnil &&
      (TYPE(rb_timeout) != T_FIXNUM || (FIX2INT(rb_timeout) > 255) ||
       (FIX2INT(rb_timeout) < 0))) {
    rb_raise(rb_eTypeError,
             "timeout isn't a valid number (any number from 0 to 255).\n       "
             " 0 == no timeout.");
    return Qnil;
  }
  // validate process count and set limits
  if (rb_processes != Qnil && TYPE(rb_processes) != T_FIXNUM &&
      FIX2INT(rb_processes) > 32) {
    rb_raise(rb_eTypeError, "processes isn't a valid number (1-32).");
    return Qnil;
  }
  // validate thread count and set limits
  if (rb_threads != Qnil && TYPE(rb_threads) != T_FIXNUM &&
      FIX2INT(rb_threads) > 128) {
    rb_raise(rb_eTypeError, "threads isn't a valid number (-1 to 128).");
    return Qnil;
  }
  // make port into a CString (for Lib-Server)
  char port[7];
  char* bind = rb_bind == Qnil ? NULL : StringValueCStr(rb_bind);
  unsigned char timeout = rb_timeout == Qnil ? 2 : FIX2INT(rb_timeout);
  snprintf(port, 6, "%d", iport);
  // create the HttpProtocol object
  struct HttpProtocol http_protocol = HttpProtocol();
  http_protocol.on_request = on_request;
  // http_protocol.maximum_body_size = xxx;

  // setup the server
  struct ServerSettings settings = {
      .protocol = (struct Protocol*)(&http_protocol),
      .timeout = timeout,
      .threads = rb_threads == Qnil ? 0 : (FIX2INT(rb_threads)),
      .processes = rb_processes == Qnil ? 0 : (FIX2INT(rb_processes)),
      .on_init = on_init,
      .on_idle = on_idle,
      .port = (iport > 0 ? port : NULL),
      .address = bind,
      .udata = (void*)self,
      .busy_msg =
          "HTTP/1.1 500 Server Too Busy\r\n"
          "Connection: closed\r\n"
          "Content-Length: 15\r\n\r\n"
          "Server Too Busy\r\n",
  };
  // setup some Rack dynamic values
  R_MTHREAD_V = settings.threads > 1 ? Qtrue : Qfalse;
  R_MPROCESS_V = settings.processes > 1 ? Qtrue : Qfalse;
  // rb_thread_call_without_gvl(slow_func, slow_arg, unblck_func,
  // unblck_arg);
  rb_thread_call_without_gvl(srv_start_no_gvl, &settings, unblck, NULL);
  return self;
}

/////////////////////////////
// stuff related to the core inheritance.

// prevent protocol changes
static VALUE http_protocol_get(VALUE self) {
  rb_warn(
      "The Iodine HTTP protocol is written in C, it cannot be edited nor "
      "viewed in Ruby.");
  return self;
}
static VALUE http_protocol_set(VALUE self, VALUE _) {
  return http_protocol_get(self);
}
/////////////////////////////
// initialize the class and the whole of the Iodine/http library
void Init_iodine_http(void) {
  // get IDs and data that's used often
  BinaryEncodingIndex = rb_enc_find_index("binary");  // sets encoding for data
  BinaryEncoding = rb_enc_find("binary");             // sets encoding for data
  call_proc_id = rb_intern("call");     // used to call the main callback
  server_var_id = rb_intern("server");  // when upgrading
  fd_var_id = rb_intern("sockfd");      // when upgrading
  each_method_id = rb_intern("each");   // for the response
  to_s_method_id = rb_intern("to_s");   // for the response

  // some common Rack & Http strings
  REQUEST_METHOD = rb_enc_str_new_literal("REQUEST_METHOD", BinaryEncoding);
  rb_global_variable(&REQUEST_METHOD);
  rb_obj_freeze(REQUEST_METHOD);
  CONTENT_TYPE = rb_enc_str_new_literal("CONTENT_TYPE", BinaryEncoding);
  rb_global_variable(&CONTENT_TYPE);
  rb_obj_freeze(CONTENT_TYPE);
  CONTENT_LENGTH = rb_enc_str_new_literal("CONTENT_LENGTH", BinaryEncoding);
  rb_global_variable(&CONTENT_LENGTH);
  rb_obj_freeze(CONTENT_LENGTH);
  SCRIPT_NAME = rb_enc_str_new_literal("SCRIPT_NAME", BinaryEncoding);
  rb_global_variable(&SCRIPT_NAME);
  rb_obj_freeze(SCRIPT_NAME);
  PATH_INFO = rb_enc_str_new_literal("PATH_INFO", BinaryEncoding);
  rb_global_variable(&PATH_INFO);
  rb_obj_freeze(PATH_INFO);
  QUERY_STRING = rb_enc_str_new_literal("QUERY_STRING", BinaryEncoding);
  rb_global_variable(&QUERY_STRING);
  rb_obj_freeze(QUERY_STRING);
  QUERY_ESTRING = rb_str_buf_new(0);
  rb_global_variable(&QUERY_ESTRING);
  rb_obj_freeze(QUERY_ESTRING);
  SERVER_NAME = rb_enc_str_new_literal("SERVER_NAME", BinaryEncoding);
  rb_global_variable(&SERVER_NAME);
  rb_obj_freeze(SERVER_NAME);
  SERVER_PORT = rb_enc_str_new_literal("SERVER_PORT", BinaryEncoding);
  rb_global_variable(&SERVER_PORT);
  rb_obj_freeze(SERVER_PORT);
  SERVER_PORT_80 = rb_enc_str_new_literal("80", BinaryEncoding);
  rb_global_variable(&SERVER_PORT_80);
  rb_obj_freeze(SERVER_PORT_80);
  SERVER_PORT_443 = rb_enc_str_new_literal("443", BinaryEncoding);
  rb_global_variable(&SERVER_PORT_443);
  rb_obj_freeze(SERVER_PORT_443);
  R_VERSION = rb_enc_str_new_literal("rack.version", BinaryEncoding);
  rb_global_variable(&R_VERSION);
  rb_obj_freeze(R_VERSION);
  R_SCHEME = rb_enc_str_new_literal("rack.url_scheme", BinaryEncoding);
  rb_global_variable(&R_SCHEME);
  rb_obj_freeze(R_SCHEME);
  R_SCHEME_HTTP = rb_enc_str_new_literal("http", BinaryEncoding);
  rb_global_variable(&R_SCHEME_HTTP);
  rb_obj_freeze(R_SCHEME_HTTP);
  R_SCHEME_HTTPS = rb_enc_str_new_literal("https", BinaryEncoding);
  rb_global_variable(&R_SCHEME_HTTPS);
  rb_obj_freeze(R_SCHEME_HTTPS);
  R_INPUT = rb_enc_str_new_literal("rack.input", BinaryEncoding);
  rb_global_variable(&R_INPUT);
  rb_obj_freeze(R_INPUT);
  R_ERRORS = rb_enc_str_new_literal("rack.errors", BinaryEncoding);
  rb_global_variable(&R_ERRORS);
  rb_obj_freeze(R_ERRORS);
  R_MTHREAD = rb_enc_str_new_literal("rack.multithread", BinaryEncoding);
  rb_global_variable(&R_MTHREAD);
  rb_obj_freeze(R_MTHREAD);
  R_MPROCESS = rb_enc_str_new_literal("rack.multiprocess", BinaryEncoding);
  rb_global_variable(&R_MPROCESS);
  rb_obj_freeze(R_MPROCESS);
  R_RUN_ONCE = rb_enc_str_new_literal("rack.run_once", BinaryEncoding);
  rb_global_variable(&R_RUN_ONCE);
  rb_obj_freeze(R_RUN_ONCE);
  R_HIJACK_Q = rb_enc_str_new_literal("rack.hijack?", BinaryEncoding);
  rb_global_variable(&R_HIJACK_Q);
  rb_obj_freeze(R_HIJACK_Q);
  R_HIJACK_Q_V = Qfalse;
  R_HIJACK = rb_enc_str_new_literal("rack.hijack", BinaryEncoding);
  rb_global_variable(&R_HIJACK);
  rb_obj_freeze(R_HIJACK);
  R_HIJACK_V = Qnil;  // rb_fdopen(int fd, const char *modestr)
  R_HIJACK_IO = rb_enc_str_new_literal("rack.hijack_io", BinaryEncoding);
  rb_global_variable(&R_HIJACK_IO);
  rb_obj_freeze(R_HIJACK_IO);
  R_HIJACK_IO_V = Qnil;
  // open the Iodine class
  rIodine = rb_define_class("Iodine", rb_cObject);
  // setup for Rack.
  VALUE version_val = rb_const_get(rIodine, rb_intern("VERSION"));
  R_VERSION_V = rb_str_split(version_val, ".");
  rb_global_variable(&R_VERSION_V);
  R_ERRORS_V = rb_stdout;

  // define the Server class - for upgrades
  rServer = rb_define_class_under(rIodine, "ServerObject", rb_cData);
  rb_global_variable(&rServer);
  // define the Http class
  rHttp = rb_define_class_under(rIodine, "Http", rIodine);
  rb_global_variable(&rHttp);
  // add the Http sub-functions
  rb_define_method(rHttp, "protocol=", http_protocol_set, 1);
  rb_define_method(rHttp, "protocol", http_protocol_get, 0);
  rb_define_method(rHttp, "start", http_start, 0);
  rb_define_attr(rHttp, "on_request", 1, 1);
  // initialize the RackIO class
  RackIO.init(rHttp);
}
