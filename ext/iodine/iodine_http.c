#include "iodine_http.h"

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

/* ////////////////////////////////////////////////////////////

The main class - Iodine::Http

//////////////////////////////////////////////////////////// */

/////////////////////////////
// server callbacks

// called when the server starts up. Saves the server object to the instance.
static void on_init(struct Server* server) {
  VALUE core_instance = ((VALUE)Server.settings(server)->udata);
  // save the updated on_request  as a global value on the server, using fd=0
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
  // rb_thread_call_without_gvl(slow_func, slow_arg, unblck_func, unblck_arg);
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
