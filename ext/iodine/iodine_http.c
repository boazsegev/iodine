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
static int BinaryEncodingIndex;  // encoding index
static VALUE rHttp;              // The Iodine::Http class
static VALUE rIodine;            // The Iodine class
static VALUE rServer;            // server object to Ruby class
static ID server_var_id;         // id for the Server variable (pointer)
static ID fd_var_id;             // id for the file descriptor (Fixnum)
static ID call_proc_id;          // id for `#call`
// for Rack
VALUE CONTENT_TYPE;     // for Rack.
VALUE CONTENT_LENGTH;   // for Rack.
VALUE SCRIPT_NAME;      // for Rack
VALUE PATH_INFO;        // for Rack
VALUE QUERY_STRING;     // for Rack
VALUE QUERY_ESTRING;    // for rack (if no query)
VALUE SERVER_NAME;      // for Rack
VALUE SERVER_PORT;      // for Rack
VALUE SERVER_PORT_80;   // for Rack
VALUE SERVER_PORT_443;  // for Rack
VALUE R_VERSION;        // for Rack: rack.version
VALUE R_VERSION_V;      // for Rack: rack.version
VALUE R_SCHEME;         // for Rack: rack.url_scheme
VALUE R_SCHEME_HTTP;    // for Rack: rack.url_scheme value
VALUE R_SCHEME_HTTPS;   // for Rack: rack.url_scheme value
VALUE R_INPUT;          // for Rack: rack.input
VALUE R_ERRORS;         // for Rack: rack.errors
VALUE R_ERRORS_V;       // for Rack: rack.errors
VALUE R_MTHREAD;        // for Rack: rack.multithread
VALUE R_MTHREAD_V;      // for Rack: rack.multithread
VALUE R_MPROCESS;       // for Rack: rack.multiprocess
VALUE R_MPROCESS_V;     // for Rack: rack.multiprocess
VALUE R_RUN_ONCE;       // for Rack: rack.run_once
VALUE R_HIJACK_Q;       // for Rack: rack.hijack?
VALUE R_HIJACK_Q_V;     // for Rack: rack.hijack?
VALUE R_HIJACK;         // for Rack: rack.hijack
VALUE R_HIJACK_V;       // for Rack: rack.hijack
VALUE R_HIJACK_IO;      // for Rack: rack.hijack_io
VALUE R_HIJACK_IO_V;    // for Rack: rack.hijack_io

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
  return Qnil;
  // Create the env Hash
  VALUE env = rb_hash_new();
  // Register the object
  Registry.add(env);
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
  // set the simple core settings
  rb_hash_aset(env, PATH_INFO, rb_str_new_cstr(request->path));
  rb_hash_aset(
      env, QUERY_STRING,
      (request->query ? rb_str_new_cstr(request->query) : QUERY_ESTRING));
  // set scheme R_SCHEME_HTTP  or R_SCHEME_HTTPS or dynamic
  int ssl = 0;
  {
    if (HttpRequest.find(request, "x-forwarded-proto")) {
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
    } else if (HttpRequest.find(request, "forwarded")) {
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
      rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
    }
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
    if (pos < len)
      rb_hash_aset(env, SERVER_PORT,
                   rb_str_new(request->host + pos, len - pos));
    else
      rb_hash_aset(env, SERVER_PORT, (ssl ? SERVER_PORT_443 : SERVER_PORT_80));
  }
  // set POST data (todo)
  if (request->content_type) {
    rb_hash_aset(env, CONTENT_TYPE, rb_str_new_cstr(request->content_type));
  }
  if (request->content_length) {
    rb_hash_aset(env, CONTENT_LENGTH, INT2FIX(request->content_length));
  }
  if (request->body_str) {
    /* todo: make request->body_str into an IO stream */
    int todo;
    rb_hash_aset(env, R_INPUT, rb_str_new_cstr(request->body_str));
  } else if (request->body_file) {
    /* todo: make request->body_file into an IO stream */
    int todo;
  } else {
    /* todo: make an empty IO stream */
    int todo;
  }

  // itterate through the headers and set the HTTP_X "variables"
  HttpRequest.first(request);
}
// translate a struct HttpRequest to a Hash, according top the
// Rack specifications.
static void send_response(struct HttpRequest* request, VALUE response) {
  int todo;
  return;
}

// The core handler passed on to the HttpProtocol object.
static void on_request(struct HttpRequest* request) {
  VALUE env = request_to_env(request);
  // a regular request is forwarded to the on_request callback.
  ;
  VALUE response = RubyCaller.call2((VALUE)Server.get_udata(request->server, 0),
                                    call_proc_id, 1, &env);
  send_response(request, response);
  // clean-up
  Registry.remove(env);
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
  VALUE on_request = rb_iv_get(self, "@on_request");
  if (rb_obj_method(on_request, ID2SYM(call_proc_id)) == Qnil) {
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
  // http_protocol.on_request = xxx;
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
  call_proc_id = rb_intern("call");     // used to call the main callback
  server_var_id = rb_intern("server");  // when upgrading
  fd_var_id = rb_intern("sockfd");      // when upgrading
  // some common Rack strings
  CONTENT_TYPE = rb_str_new_literal("CONTENT-TYPE");
  CONTENT_LENGTH = rb_str_new_literal("CONTENT-LENGTH");
  SCRIPT_NAME = rb_str_new_literal("SCRIPT_NAME");
  PATH_INFO = rb_str_new_literal("PATH_INFO");
  QUERY_STRING = rb_str_new_literal("QUERY_STRING");
  QUERY_ESTRING = rb_str_new_literal("");
  SERVER_NAME = rb_str_new_literal("SERVER_NAME");
  SERVER_PORT = rb_str_new_literal("SERVER_PORT");
  SERVER_PORT_80 = rb_str_new_literal("80");
  SERVER_PORT_443 = rb_str_new_literal("443");
  R_VERSION = rb_str_new_literal("rack.version");
  R_SCHEME = rb_str_new_literal("rack.url_scheme");
  R_SCHEME_HTTP = rb_str_new_literal("http");
  R_SCHEME_HTTPS = rb_str_new_literal("https");
  R_INPUT = rb_str_new_literal("rack.input");
  R_ERRORS = rb_str_new_literal("rack.errors");
  R_MTHREAD = rb_str_new_literal("rack.multithread");
  R_MPROCESS = rb_str_new_literal("rack.multiprocess");
  R_RUN_ONCE = rb_str_new_literal("rack.run_once");
  R_HIJACK_Q = rb_str_new_literal("rack.hijack?");
  R_HIJACK_Q_V = Qfalse;
  R_HIJACK = rb_str_new_literal("rack.hijack");
  R_HIJACK_V = Qnil;  // rb_fdopen(int fd, const char *modestr)
  R_HIJACK_IO = rb_str_new_literal("rack.hijack_io");
  R_HIJACK_IO_V = Qnil;

  // setup for Rack.
  VALUE version_val = rb_const_get(rIodine, rb_intern("VERSION"));
  R_VERSION_V = rb_str_split(version_val, ".");
  R_ERRORS_V = rb_stdout;

  // open the Iodine class
  rIodine = rb_define_class("Iodine", rb_cObject);
  // define the Server class - for upgrades
  rServer = rb_define_class_under(rIodine, "ServerObject", rb_cData);
  // define the Http class
  rHttp = rb_define_class_under(rIodine, "Http", rIodine);
  // add the Http sub-functions
  rb_define_method(rHttp, "protocol=", http_protocol_set, 1);
  rb_define_method(rHttp, "protocol", http_protocol_get, 0);
  rb_define_method(rHttp, "start", http_start, 0);
  rb_define_attr(rHttp, "on_request", 1, 1);
}
