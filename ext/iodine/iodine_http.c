#include "iodine_websocket.h"
#include "iodine_http.h"
#include <ruby.h>
#include <ruby/io.h>
#include <ruby/version.h>
#include <time.h>

/* ////////////////////////////////////////////////////////////
This file creates an HTTP server based on the Iodine libraries.

The server is (mostly) Rack compatible, except:

1. upgrade requests are handled using special upgrade handlers.
2. if a String is returned, it is a assumed to be a status 200 Html data?

//////////////////////////////////////////////////////////// */

// This one is shared
VALUE rHttp;  // The Iodine::Http class

//////////////
// general global definitions we will use herein.
static ID server_var_id;    // id for the Server variable (pointer)
static ID fd_var_id;        // id for the file descriptor (Fixnum)
static ID call_proc_id;     // id for `#call`
static ID each_method_id;   // id for `#each`
static ID close_method_id;  // id for `#close`
static ID to_s_method_id;   // id for `#to_s`
static ID new_func_id;      // id for the Class.new method
static ID on_open_func_id;  // the on_open callback's ID
static VALUE _hijack_sym;

// for Rack
static VALUE HTTP_VERSION;     // extending Rack
static VALUE REQUEST_URI;      // extending Rack
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
// these three are used also by rb-rack-io.c
VALUE R_HIJACK;                     // for Rack: rack.hijack
VALUE R_HIJACK_IO;                  // for Rack: rack.hijack_io
VALUE R_HIJACK_CB;                  // for Rack: rack.hijack_io callback
static VALUE R_IODINE_UPGRADE;      // Iodine upgrade support
static VALUE R_IODINE_UPGRADE_DYN;  // Iodine upgrade support
static VALUE UPGRADE_HEADER;        // upgrade support
static VALUE UPGRADE_HEADER;        // upgrade support
static VALUE CONNECTION_HEADER;     // upgrade support
static VALUE CONNECTION_CLOSE;      // upgrade support
static VALUE WEBSOCKET_STR;         // upgrade support
static VALUE WEBSOCKET_VER;         // upgrade support
static VALUE WEBSOCKET_SEC_VER;     // upgrade support
static VALUE WEBSOCKET_SEC_EXT;     // upgrade support
static VALUE WEBSOCKET_SEC_ACPT;    // upgrade support
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
// SCRIPT_NAME is empty. SCRIPT_NAME never should be /, but instead be
// empty.

/* ////////////////////////////////////////////////////////////

The Http on_request handling functions

//////////////////////////////////////////////////////////// */

// translate a struct HttpRequest to a Hash, according top the
// Rack specifications.
static VALUE request_to_env(struct HttpRequest* request) {
  // return Qnil;
  // Create the env Hash
  VALUE env = rb_hash_new();
  VALUE tmp = 0;
  // Register the object
  Registry.add(env);
  // set the simple core request data
  rb_hash_aset(
      env, REQUEST_METHOD,
      rb_enc_str_new(request->method, strlen(request->method), BinaryEncoding));
  tmp = rb_enc_str_new(request->path, strlen(request->path), BinaryEncoding);
  rb_hash_aset(env, PATH_INFO, tmp);
  rb_hash_aset(env, REQUEST_URI, tmp);
  rb_hash_aset(
      env, QUERY_STRING,
      (request->query ? rb_enc_str_new(request->query, strlen(request->query),
                                       BinaryEncoding)
                      : QUERY_ESTRING));
  rb_hash_aset(env, HTTP_VERSION,
               rb_enc_str_new(request->version, strlen(request->version),
                              BinaryEncoding));

  // setup static env data
  rb_hash_aset(env, R_VERSION, R_VERSION_V);
  rb_hash_aset(env, SCRIPT_NAME, QUERY_ESTRING);
  rb_hash_aset(env, R_ERRORS, R_ERRORS_V);
  rb_hash_aset(env, R_MTHREAD, R_MTHREAD_V);
  rb_hash_aset(env, R_MPROCESS, R_MPROCESS_V);
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
  char* content_length_header = NULL;
  if (request->content_length) {
    // check for content type
    if (request->content_type) {
      rb_hash_aset(
          env, CONTENT_TYPE,
          rb_enc_str_new(request->content_type, strlen(request->content_type),
                         BinaryEncoding));
    }
    // CONTENT_LENGTH should be a string (damn stupid Rack specs).
    // find already showed success, no need for `if`
    HttpRequest.find(request, "CONTENT-LENGTH");
    content_length_header = HttpRequest.name(request);
    char* value = HttpRequest.value(request);
    rb_hash_aset(env, CONTENT_LENGTH,
                 rb_enc_str_new(value, strlen(value), BinaryEncoding));
  }
  rb_hash_aset(env, R_INPUT, (tmp = RackIO.new(request, env)));
  // setup Hijacking headers
  //   rb_hash_aset(env, R_HIJACK_Q, Qfalse);
  VALUE hj_method = rb_obj_method(tmp, _hijack_sym);
  rb_hash_aset(env, R_HIJACK_Q, Qtrue);
  rb_hash_aset(env, R_HIJACK, hj_method);
  rb_hash_aset(env, R_HIJACK_IO, Qnil);
  rb_hash_aset(env, R_IODINE_UPGRADE, Qnil);
  rb_hash_aset(env, R_IODINE_UPGRADE_DYN, Qnil);
  // itterate through the headers and set the HTTP_X "variables"
  // we will do so destructively (overwriting the Parser's data)
  HttpRequest.first(request);
  {
    char *name, *value, *tmp;
    VALUE header;
    do {
      tmp = name = HttpRequest.name(request);
      value = HttpRequest.value(request);
      // careful, pointer comparison crashed Ruby (although it works without
      // Ruby)... this could be an issue.
      if (value == (request->content_type) || (name == content_length_header))
        continue;

      // replace '-' with '_' for header name
      while (*(++tmp))
        if (*tmp == '-')
          *tmp = '_';
      header = rb_sprintf("HTTP_%s", name);
      rb_enc_associate(header, BinaryEncoding);
      rb_hash_aset(env, header,
                   rb_enc_str_new(value, strlen(value), BinaryEncoding));
      // undo the change ('_' -> '-')
      tmp = name;
      while (*(++tmp))
        if (*tmp == '_')
          *tmp = '-';
    } while (HttpRequest.next(request));
  }
  HttpRequest.first(request);
  return env;
}

// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static int for_each_header_data(VALUE key, VALUE val, VALUE _res) {
  // fprintf(stderr, "For_each - headers\n");
  struct HttpResponse* response = (void*)_res;
  if (TYPE(key) != T_STRING)
    key = RubyCaller.call(key, to_s_method_id);
  if (TYPE(key) != T_STRING)
    return ST_CONTINUE;
  if (TYPE(val) != T_STRING) {
    val = RubyCaller.call(val, to_s_method_id);
    if (TYPE(val) != T_STRING)
      return ST_STOP;
  }
  char* key_s = RSTRING_PTR(key);
  int key_len = RSTRING_LEN(key);
  char* val_s = RSTRING_PTR(val);
  int val_len = RSTRING_LEN(val);
  // scan the value for newline (\n) delimiters
  int pos_s = 0, pos_e = 0;
  while (pos_e < val_len) {
    // scanning for newline (\n) delimiters
    while (pos_e < val_len && val_s[pos_e] != '\n')
      pos_e++;
    HttpResponse.write_header(response, key_s, key_len, val_s + pos_s,
                              pos_e - pos_s);
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
  // fprintf(stderr, "For_each - body\n");
  struct HttpResponse* response = (void*)_res;
  // write body
  if (TYPE(str) != T_STRING) {
    fprintf(stderr,
            "Iodine Server Error:"
            "response body was not a String\n");
    return Qfalse;
  }
  if (RSTRING_LEN(str)) {
    if (HttpResponse.write_body(response, RSTRING_PTR(str), RSTRING_LEN(str))) {
      // fprintf(stderr,
      //         "Iodine Server Error:"
      //         "couldn't write response to connection\n");
      return Qfalse;
    }
  } else {
    if (HttpResponse.send(response)) {
      // fprintf(stderr,
      //         "Iodine Server Error:"
      //         "couldn't write response to connection\n");
      return Qfalse;
    }
  }
  return Qtrue;
}

// Gets the response object, within a GVL context
static void* handle_request_in_gvl(void* _req) {
  struct HttpRequest* request = _req;
  struct HttpResponse* response = NULL;
  VALUE env = request_to_env(request);
  // Registry.add(env); // performed by the request_to_env function
  VALUE rb_response = (VALUE)Server.get_udata(request->server, 0);
  if (!rb_response) {
    fprintf(stderr, "* No Ruby Response handler\n");
    goto internal_error;
  }
  rb_response = RubyCaller.call2(rb_response, call_proc_id, 1, &env);
  if (rb_response == Qnil || (void*)rb_response == NULL ||
      rb_response == Qfalse) {
    rb_response = 0;
    goto internal_error;
  }
  Registry.add(rb_response);
  // fprintf(stderr, "added response to registry\n");
  /////////////// we now have the response object ready. Time to work...

  // // // Review pre-response Upgrade(s) or Hijacking
  VALUE handler;  // will hold the upgrade object
  if ((handler = rb_hash_aref(env, R_IODINE_UPGRADE)) != Qnil) {
    // websocket upgrade.
    // fprintf(stderr, "ws upgrade\n");
    // we're done with the `handler` variable for now, so we can use as tmp.
    handler = rb_ary_entry(rb_response, 2);
    // close the body, if it exists.
    if (rb_respond_to(handler, close_method_id))
      RubyCaller.call(handler, close_method_id);
    // no body will be sent
    rb_ary_store(rb_response, 2, Qnil);
    handler = Qnil;
  } else if ((handler = rb_hash_aref(env, R_HIJACK_IO)) != Qnil) {
    // Hijack now.
    // fprintf(stderr, "Hijacked\n");
    Server.hijack(request->server, request->sockfd);
    goto cleanup;
  } else if ((handler = rb_hash_aref(env, R_IODINE_UPGRADE_DYN)) != Qnil) {
    // generic upgrade.
    // fprintf(stderr, "Generic Upgrade\n");
    // include the rDynProtocol within the object.
    if (TYPE(handler) == T_CLASS) {
      // include the Protocol module
      rb_include_module(handler, rDynProtocol);
      handler = RubyCaller.call(handler, new_func_id);
    } else {
      // include the Protocol module in the object's class
      VALUE p_class = rb_obj_class(handler);
      // // do we neet to check?
      // if (rb_mod_include_p(p_class, rDynProtocol) == Qfalse)
      rb_include_module(p_class, rDynProtocol);
    }
    // add new protocol to the Registry - should be removed in on_close
    Registry.add(handler);
    // set the new protocol as the connection's udata
    Server.set_udata(request->server, request->sockfd, (void*)handler);
    // initialize pre-required variables
    rb_ivar_set(handler, fd_var_id, INT2FIX(request->sockfd));
    set_server(handler, request->server);
    // switch from HTTP to a dynamic protocol
    Server.set_protocol(request->server, request->sockfd, &DynamicProtocol);
    // initialize the new protocol
    RubyCaller.call(handler, on_open_func_id);
    goto cleanup;
  }
  // fprintf(stderr, "grabbing response\n");
  // // // prep and send the HTTP response
  response = HttpResponse.create(request);
  if (!response)
    goto cleanup;
  // fprintf(stderr, "grabbing body\n");
  VALUE body = rb_ary_entry(rb_response, 2);

  // extract the hijack callback header from the response, if it exists...
  if ((handler = rb_hash_aref(rb_ary_entry(rb_response, 1), R_HIJACK_CB)) !=
          Qnil ||
      (handler = rb_hash_aref(env, R_HIJACK_CB)) != Qnil) {
    // fprintf(stderr, "hijack callback review\n");
    rb_hash_delete(rb_ary_entry(rb_response, 1), R_HIJACK_CB);
    // close the body, if it exists.
    if (rb_respond_to(body, close_method_id))
      RubyCaller.call(body, close_method_id);
    // no body will be sent
    rb_ary_store(rb_response, 2, Qnil);
    body = Qnil;
  }
  // set status
  {
    // fprintf(stderr, "set status\n");
    VALUE tmp = rb_ary_entry(rb_response, 0);
    if (TYPE(tmp) == T_STRING)
      tmp = rb_str_to_inum(tmp, 10, 0);
    if (TYPE(tmp) == T_FIXNUM)
      response->status = FIX2INT(tmp);
  }

  // fprintf(stderr, "Write headers\n");
  // write headers
  rb_hash_foreach(rb_ary_entry(rb_response, 1), for_each_header_data,
                  (VALUE)response);

  // handle body
  if (TYPE(body) == T_ARRAY && RARRAY_LEN(body) == 1) {  // [String] is likely
    body = rb_ary_entry(body, 0);
    // fprintf(stderr, "Body was a single item array, unpacket to string\n");
  }

  if (response->status < 200 || response->status == 204 ||
      response->status == 304) {
    if (rb_respond_to(body, close_method_id))
      RubyCaller.call(body, close_method_id);
    body = Qnil;
    response->content_length = -1;
  }

  if (TYPE(body) == T_STRING) {
    // fprintf(stderr, "Review body as String\n");
    if (RSTRING_LEN(body))
      HttpResponse.write_body(response, RSTRING_PTR(body), RSTRING_LEN(body));
    else
      HttpResponse.send(response);
  } else if (body == Qnil) {
    // fprintf(stderr, "Review body as nil\n");
    // This could be a websocket/upgrade/hijack - review post-response
    if (handler != Qnil) {
      // Post response Hijack, send response and hijack
      HttpResponse.send(response);
      // Hijack and call callback. TODO: test this
      body = rb_hash_aref(env, R_HIJACK);                 // use `body` as `tmp`
      body = RubyCaller.call(body, call_proc_id);         // grab the IO
      RubyCaller.call2(handler, call_proc_id, 1, &body);  // call the callback
      // cleanup
      goto cleanup;
    } else if ((handler = rb_hash_aref(env, R_IODINE_UPGRADE)) != Qnil) {
      // perform websocket upgrade.
      iodine_websocket_upgrade(request, response, handler);
      goto cleanup;
    } else
      HttpResponse.send(response);
  } else if (rb_respond_to(body, each_method_id)) {
    // fprintf(stderr, "Review body as for-each ...\n");
    if (!response->metadata.connection_written &&
        !response->metadata.connection_len_written) {
      // close the connection to indicate message length...
      // protection from bad code
      response->metadata.should_close = 1;
      response->content_length = -1;
    }
    rb_block_call(body, each_method_id, 0, NULL, for_each_body_string,
                  (VALUE)response);
    // make sure the response is sent even if it was an empty collection
    HttpResponse.send(response);
    // we need to call `close` in case the object is an IO / BodyProxy
    if (rb_respond_to(body, close_method_id))
      RubyCaller.call(body, close_method_id);
  } else {
    HttpResponse.reset(response, request);
    goto internal_error;
  }

cleanup:
  if (response) {
    // TODO log ?
    // fprintf(stderr, "[%llu] : %s - %s : %d\n", request->sockfd,
    // request->method,
    //         request->path, response->status);
    // destroy response
    HttpResponse.destroy(response);
  }
  if (rb_response)
    Registry.remove(rb_response);
  Registry.remove(env);
  return 0;
internal_error:
  response = HttpResponse.create(request);
  if (!response)
    Server.close(request->server, request->sockfd);
  response->status = 500;
  response->metadata.should_close = 1;
  HttpResponse.write_body(response, "Internal error.", 15);
  // TODO log ?
  // fprintf(stderr, "%s - %s : %d\n", request->method, request->path,
  //         response->status);
  HttpResponse.destroy(response);
  return 0;
}

// The core handler passed on to the HttpProtocol object.
static void on_request(struct HttpRequest* request) {
  // work inside the GVL
  RubyCaller.call_c(handle_request_in_gvl, request);
}

/* ////////////////////////////////////////////////////////////

The main class - Iodine::Http

//////////////////////////////////////////////////////////// */

/////////////////////////////
// server callbacks

// called when the server starts up. Saves the server object to the
// instance.
static void on_init(server_pt server) {
  VALUE self = ((VALUE)Server.settings(server)->udata);
  // save the updated on_request  as a global value on the server, using
  // fd=0
  if (rb_iv_get(self, "@on_http") != Qnil)
    Server.set_udata(server, 0, (void*)rb_iv_get(self, "@on_http"));
  // set the server variable in the core server object.. is this GC safe?
  set_server(self, server);
  // HTTP timeout
  VALUE rb_timeout = rb_ivar_get(self, rb_intern("@timeout"));
  if (rb_timeout != Qnil)
    Server.settings(server)->timeout = FIX2INT(rb_timeout);

  // setup HTTP limits
  VALUE rb_max_body = rb_ivar_get(self, rb_intern("@max_body_size"));
  if (rb_max_body != Qnil)
    ((struct HttpProtocol*)Server.settings(server)->protocol)
        ->maximum_body_size = FIX2INT(rb_max_body);
  // setup websocket settings
  VALUE rb_max_msg = rb_ivar_get(self, rb_intern("@max_msg_size"));
  if (rb_max_msg != Qnil)
    Websocket.max_msg_size = FIX2INT(rb_max_msg);
  VALUE rb_ws_tout = rb_ivar_get(self, rb_intern("@ws_timeout"));
  if (rb_ws_tout != Qnil)
    Websocket.timeout = FIX2INT(rb_ws_tout);

  // perform on_init callback - we don't need the self variable, we'll
  // recycle it.
  VALUE start_ary = rb_iv_get(self, "on_start_array");
  if (start_ary != Qnil) {
    while ((self = rb_ary_pop(start_ary)) != Qnil) {
      RubyCaller.call(self, call_proc_id);
    }
  }
}

/////////////////////////////
// Running the Http server
//
// the no-GVL state
static void* srv_start_no_gvl(void* _self) {
  VALUE self = (VALUE)_self;

  // make port into a CString (for Lib-Server)
  char port[7];
  VALUE rb_port = rb_ivar_get(self, rb_intern("@port"));
  int iport = rb_port == Qnil ? 3000 : FIX2INT(rb_port);
  snprintf(port, 6, "%d", iport);
  // bind address
  VALUE rb_bind = rb_ivar_get(self, rb_intern("@address"));
  char* bind = rb_bind == Qnil ? NULL : StringValueCStr(rb_bind);

  // concurrency
  VALUE rb_threads = rb_ivar_get(self, rb_intern("@threads"));
  int threads = rb_threads == Qnil ? 1 : (FIX2INT(rb_threads));
  VALUE rb_processes = rb_ivar_get(self, rb_intern("@processes"));
  int processes = rb_processes == Qnil ? 1 : (FIX2INT(rb_processes));

  // Public folder
  VALUE rb_public_folder = rb_ivar_get(self, rb_intern("@public_folder"));
  char* public_folder =
      (rb_public_folder == Qnil ? NULL : StringValueCStr(rb_public_folder));

  // Write message
  VALUE iodine_version = rb_const_get(rIodine, rb_intern("VERSION"));
  VALUE ruby_version = rb_const_get(rIodine, rb_intern("RUBY_VERSION"));
  fprintf(stderr,
          "Starting up Iodine Http Server:\n"
          " * Ruby v.%s\n * Iodine v.%s \n"
          " * %d thread%s X %d processes\n\n",
          StringValueCStr(ruby_version), StringValueCStr(iodine_version),
          threads, (threads > 1 ? "s" : ""), processes);

  // Start Http Server
  start_http_server(
      on_request, public_folder, .threads = threads, .processes = processes,
      .on_init = on_init, .on_idle = on_idle_server_callback,
      .port = (iport > 0 ? port : NULL), .address = bind, .udata = (void*)self);
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
  // review the callbacks
  VALUE on_http_handler = rb_iv_get(self, "@on_http");
  if (on_http_handler == Qnil) {
    rb_raise(rb_eRuntimeError, "The `on_http` callback must be defined");
    return Qnil;
  }
  // review the callback
  if (on_http_handler != Qnil &&
      !rb_respond_to(on_http_handler, call_proc_id)) {
    rb_raise(rb_eTypeError,
             "The on_http callback should be an object that answers to the "
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
  VALUE rb_max_body = rb_ivar_get(self, rb_intern("@max_body_size"));
  VALUE rb_max_msg = rb_ivar_get(self, rb_intern("@max_msg_size"));
  VALUE rb_ws_tout = rb_ivar_get(self, rb_intern("@ws_timeout"));
  VALUE rb_public_folder = rb_ivar_get(self, rb_intern("@public_folder"));
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
  // validate body size limits
  int imax_body = rb_max_body == Qnil ? 32 : FIX2INT(rb_max_body);
  if (imax_body > 2048 || imax_body < 0) {
    rb_raise(rb_eTypeError,
             "max_body_size out of range. should be lo less then 0 and no more "
             "then 2048 (2Gb).");
    return Qnil;
  }
  // validate ws message size limits
  int imax_msg = rb_max_msg == Qnil ? 65536 : FIX2INT(rb_max_msg);
  if (imax_msg > 2097152 || imax_msg < 0) {
    rb_raise(rb_eTypeError,
             "max_msg_size out of range. should be lo less then 0 and no more "
             "then 2,097,152 (2Mb). Default is 64Kb");
    return Qnil;
  }
  // validate ws timeout
  int iwstout = rb_ws_tout == Qnil ? 45 : FIX2INT(rb_ws_tout);
  if (iwstout > 120 || iwstout < 0) {
    rb_raise(rb_eTypeError,
             "ws_timeout out of range. should be lo less then 0 and no more "
             "then 120 (2 minutes). Default is 45 seconds.");
    return Qnil;
  }
  // validate the public folder type
  if (rb_public_folder != Qnil && TYPE(rb_public_folder) != T_STRING) {
    rb_raise(rb_eTypeError, "public_folder should be either a String or nil.");
    return Qnil;
  }
  // validation complete - write data to env variables before leaving GVL
  // start server
  R_MTHREAD_V =
      (rb_threads == Qnil ? 1 : (FIX2INT(rb_threads))) > 1 ? Qtrue : Qfalse;
  R_MPROCESS_V =
      (rb_processes == Qnil ? 1 : (FIX2INT(rb_processes))) > 1 ? Qtrue : Qfalse;
  rb_thread_call_without_gvl2(srv_start_no_gvl, (void*)self, unblck, NULL);
  return self;
}

/////////////////////////////
// stuff related to the core inheritance.

// Stub getter/setter to prevent protocol changes on the HTTP server class.
static VALUE http_protocol_get(VALUE self) {
  rb_warn(
      "The Iodine HTTP protocol is written in C, it cannot be edited nor "
      "viewed in Ruby.");
  return self;
}
// Stub getter/setter to prevent protocol changes on the HTTP server class.
static VALUE http_protocol_set(VALUE self, VALUE _) {
  return http_protocol_get(self);
}

/////////////////////////////
// We'll be doing a lot of this - storing frozen strings into global objects
//
// Using `rb_enc_str_new_literal` is wonderful, but requires Ruby 2.2
// So it was replaced with `rb_enc_str_new_cstr` ... which required Ruby 2.1
// So it was replaced with:
//     rb_enc_str_new(const char *ptr, long len, rb_encoding *enc)
// OR:
//     rb_str_new_cstr(const char *ptr);
//     rb_enc_associate_index(accpt_str, BinaryEncodingIndex)
#define _STORE_(var, str)                                       \
  (var) = rb_enc_str_new((str), strlen((str)), BinaryEncoding); \
  rb_global_variable(&(var));                                   \
  rb_obj_freeze(var);
/////////////////////////////
// initialize the class and the whole of the Iodine/http library
void Init_iodine_http(void) {
  // get IDs and data that's used often
  call_proc_id = rb_intern("call");            // used to call the main callback
  server_var_id = rb_intern("server");         // when upgrading
  fd_var_id = rb_intern("sockfd");             // when upgrading
  new_func_id = rb_intern("new");              // when upgrading
  on_open_func_id = rb_intern("on_open");      // when upgrading
  each_method_id = rb_intern("each");          // for the response
  to_s_method_id = rb_intern("to_s");          // for the response
  close_method_id = rb_intern("close");        // for the response
  _hijack_sym = ID2SYM(rb_intern("_hijack"));  // for hijacking
  rb_global_variable(&_hijack_sym);

  // some common Rack & Http strings
  _STORE_(REQUEST_METHOD, "REQUEST_METHOD");
  _STORE_(CONTENT_TYPE, "CONTENT_TYPE");
  _STORE_(CONTENT_LENGTH, "CONTENT_LENGTH");
  _STORE_(SCRIPT_NAME, "SCRIPT_NAME");
  _STORE_(PATH_INFO, "PATH_INFO");
  _STORE_(QUERY_STRING, "QUERY_STRING");
  _STORE_(QUERY_ESTRING, "");
  _STORE_(SERVER_NAME, "SERVER_NAME");
  _STORE_(SERVER_PORT, "SERVER_PORT");
  _STORE_(SERVER_PORT_80, "80");
  _STORE_(SERVER_PORT_443, "443");
  // non-standard but often used
  _STORE_(HTTP_VERSION, "HTTP_VERSION");
  _STORE_(REQUEST_URI, "REQUEST_URI");
  // back to Rack standard
  _STORE_(R_VERSION, "rack.version");
  _STORE_(R_SCHEME, "rack.url_scheme");
  _STORE_(R_SCHEME_HTTP, "http");
  _STORE_(R_SCHEME_HTTPS, "https");
  _STORE_(R_INPUT, "rack.input");
  _STORE_(R_ERRORS, "rack.errors");
  _STORE_(R_MTHREAD, "rack.multithread");
  _STORE_(R_MPROCESS, "rack.multiprocess");
  _STORE_(R_RUN_ONCE, "rack.run_once");
  _STORE_(R_HIJACK, "rack.hijack");
  _STORE_(R_HIJACK_Q, "rack.hijack?");
  _STORE_(R_HIJACK_IO, "rack.hijack_io");
  _STORE_(R_HIJACK_CB, "iodine.hijack_cb");  // implementation specific
  // websocket upgrade
  _STORE_(R_IODINE_UPGRADE, "iodine.websocket");     // implementation specific
  _STORE_(R_IODINE_UPGRADE_DYN, "iodine.protocol");  // implementation specific
  _STORE_(UPGRADE_HEADER, "Upgrade");
  _STORE_(CONNECTION_HEADER, "connection");
  _STORE_(CONNECTION_CLOSE, "close");
  _STORE_(WEBSOCKET_STR, "websocket");
  _STORE_(WEBSOCKET_VER, "13");
  _STORE_(WEBSOCKET_SEC_VER, "sec-websocket-version");
  _STORE_(WEBSOCKET_SEC_EXT, "sec-websocket-extensions");
  _STORE_(WEBSOCKET_SEC_ACPT, "sec-websocket-accept");
  _STORE_(WEBSOCKET_STR, "websocket");

  // setup for Rack version 1.3.
  R_VERSION_V = rb_ary_new();  // rb_ary_new is Ruby 2.0 compatible
  rb_ary_push(R_VERSION_V, rb_enc_str_new("1", 1, BinaryEncoding));
  rb_ary_push(R_VERSION_V, rb_enc_str_new("3", 1, BinaryEncoding));
  rb_global_variable(&R_VERSION_V);
  R_ERRORS_V = rb_stdout;

  // define the Http class
  rHttp = rb_define_class_under(rIodine, "Http", rIodine);
  rb_global_variable(&rHttp);
  // add the Http sub-functions

  /* this overrides the default Iodine protocol method to prevent setting a
protocol.
  */
  rb_define_method(rHttp, "protocol=", http_protocol_set, 1);
  /** this overrides the default Iodine protocol method to prevent getting an
 non-existing protocol object (the protocol is written in C, not in Ruby).
   */
  rb_define_method(rHttp, "protocol", http_protocol_get, 0);
  rb_define_method(rHttp, "start", http_start, 0);
  /** The maximum body size (for posting and uploading data to the server).
Defaults to 32 Mb.
  */
  rb_define_attr(rHttp, "max_body_size", 1, 1);
  /** The maximum websocket message size.
Defaults to 64Kb.
  */
  rb_define_attr(rHttp, "max_msg_size", 1, 1);
  /** The initial websocket connection timeout (between 0-120 seconds).
Defaults to 45 seconds.
  */
  rb_define_attr(rHttp, "ws_timeout", 1, 1);
  /** The HTTP handler. This object should answer to `call(env)` (can be a
Proc).

Iodine::Http adhers to the Rack specifications and the HTTP handler should do
so as well.

The one exception, both for the `on_http` and `on_websocket` handlers, is that
when upgrading the connection, the Array can consist of 4 elements (instead of
3), the last one being a new Protocol object.

i.e.

     Iodine::Rack.on_http = Proc.new {
       [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
     }

Iodine::Http offers native websocket and protocol upgrade support.

To utilize websocket upgrade, simply provide a class or object to handle
websocket events using the callbacks and methods defined in
{Iodine::Http::WebsocketProtocol}


Here's a short example:

    class MyEcho
      def on_message data
        write data
      end
    end
    server.on_http= Proc.new do |env|
      if env["HTTP_UPGRADE".freeze] =~ /websocket/i.freeze
        env['iodine.websocket'.freeze] = WSEcho # or: WSEcho.new
        [0,{}, []] # It's possible to set cookies for the response.
      else
        [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
      end
    end

Similarly, it's easy to upgrade to a custom protocol, as specified by the
{Iodine::Protocol} mixin, by setting the `'iodine.protocol'` field. When using
this method, you either send the response by setting a valid status code or
prevent a response from being sent by setting a status code of 0 (or less).
i.e.:

    class MyEchoProtocol
      def on_message data
        # regular socket echo - NOT websockets.
        write data
      end
    end
    server.on_http= Proc.new do |env|
      if env["HTTP_UPGRADE".freeze] =~ /echo/i.freeze
        env['iodine.protocol'.freeze] = MyEchoProtocol
        [0,{}, []] # no HTTP response will be sent.
      else
        [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
      end
    end

  */
  rb_define_attr(rHttp, "on_http", 1, 1);
  /**
Allows for basic static file delivery support.

This allows static file serving to bypass the Ruby layer. If Iodine is running
behind a proxy that is capable of serving static files, such as NginX or
Apche, using that proxy will allow even better performance (under the
assumption that the added network layer's overhead could be expensive).
  */
  rb_define_attr(rHttp, "public_folder", 1, 1);
  // initialize the RackIO class
  RackIO.init();
  // initialize the Websockets class
  Init_iodine_websocket();
}
