/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_core.h"
#include "iodine_http.h"

/* *****************************************************************************
Core data
*/

/* these should be made globally accessible to any Iodine module */
rb_encoding *BinaryEncoding;
rb_encoding *UTF8Encoding;
int BinaryEncodingIndex;
int UTF8EncodingIndex;
VALUE Iodine;
VALUE IodineBase;
VALUE Iodine_Version;
const char *Iodine_Version_Str;
ID call_proc_id;
ID on_start_func_id;
ID on_finish_func_id;
ID new_func_id;
ID on_open_func_id;
ID on_message_func_id;
ID on_data_func_id;
ID on_ready_func_id;
ID on_shutdown_func_id;
ID on_close_func_id;
ID ping_func_id;
ID buff_var_id;
ID fd_var_id;
ID timeout_var_id;
ID to_s_method_id;

/* local core data variables */
static VALUE DynamicProtocol;
static VALUE DynamicProtocolClass;
/* *****************************************************************************
The Core dynamic Iodine protocol
*/

static const char *iodine_protocol_service = "IodineDynamicProtocol";

/* *****************************************************************************
The Core dynamic Iodine protocol methods and helpers
*/

/**
Reads `n` bytes from the network connection.
The number of bytes to be read (n) is:
- the number of bytes set in the optional `buffer_or_length` argument.
- the String capacity (not length) of the String passed as the optional
  `buffer_or_length` argument.
- 1024 Bytes (1Kb) if the optional `buffer_or_length` is either missing or
  contains a String who's capacity is less then 1Kb.
Returns a String (either the same one used as the buffer or a new one) on a
successful read. Returns `nil` if no data was available.
*/
static VALUE dyn_read(int argc, VALUE *argv, VALUE self) {
  if (argc > 1) {
    rb_raise(
        rb_eArgError,
        "read accepts only one argument - a Fixnum (buffer length) or a String "
        "(it's capacity - or 1Kb, whichever's the higher - will be used as "
        "buffer's length).");
    return Qnil;
  }
  VALUE buffer = (argc == 1 ? argv[0] : Qnil);
  if (buffer != Qnil && TYPE(buffer) != T_FIXNUM && TYPE(buffer) != T_STRING) {
    rb_raise(rb_eTypeError,
             "buffer should either be a length (a new string will be created) "
             "or a string (reading will be limited to the original string's "
             "capacity or 1Kb - whichever the larger).");
    return Qnil;
  }
  VALUE str;
  long len;
  intptr_t fd = iodine_get_fd(self);
  if (buffer == Qnil) {
    buffer = LONG2FIX(1024);
  }
  if (TYPE(buffer) == T_FIXNUM) {
    len = FIX2LONG(buffer);
    if (len <= 0)
      len = 1024;
    str = rb_str_buf_new(len);
    // create a rb_String with X length and take it's pointer
    // rb_str_resize(VALUE str, long len)
    // RSTRING_PTR(str)
  } else {
    // take the string's pointer and length
    len = rb_str_capacity(buffer);
    // make sure the string is modifiable
    rb_str_modify(buffer);
    // resize the string if needed.
    if (len < 1024)
      rb_str_resize(buffer, (len = 1024));
    str = buffer;
  }
  ssize_t in = sock_read(fd, RSTRING_PTR(str), len);
  // make sure it's binary encoded
  rb_enc_associate_index(str, BinaryEncodingIndex);
  // set actual size....
  if (in > 0)
    rb_str_set_len(str, (long)in);
  else {
    rb_str_set_len(str, 0);
    str = Qnil;
  }
  // return empty string? or fix above if to return Qnil?
  return str;
}

/**
Writes data to the connection. Returns `false` on error and `self` on success.
*/
static VALUE dyn_write(VALUE self, VALUE data) {
  intptr_t fd = iodine_get_fd(self);
  if (sock_write(fd, RSTRING_PTR(data), RSTRING_LEN(data)))
    return Qfalse;
  return self;
}

/**
Writes data to the connection. The data will be sent as soon as possible without
fragmantation of previously scheduled data.

Returns `false` on error and `self` on success.
*/
static VALUE dyn_write_urgent(VALUE self, VALUE data) {
  intptr_t fd = iodine_get_fd(self);
  if (sock_write2(.fduuid = fd, .buffer = RSTRING(data),
                  .length = RSTRING_LEN(data), .urgent = 1))
    return Qfalse;
  return self;
}

/**
Update's a connection's timeout.

Returns self.
*/
static VALUE dyn_set_timeout(VALUE self, VALUE timeout) {
  intptr_t fd = iodine_get_fd(self);
  unsigned int tout = FIX2UINT(timeout);
  if (tout > 255)
    tout = 255;
  server_set_timeout(fd, tout);
  return self;
}

/**
Returns the connection's timeout.
*/
static VALUE dyn_get_timeout(VALUE self) {
  intptr_t fd = iodine_get_fd(self);
  uint8_t tout = server_get_timeout(fd);
  unsigned int tout_int = tout;
  return UINT2NUM(tout_int);
}

/**
Closes a connection.

The connection will be closed only once all the data was sent.

Returns self.
*/
static VALUE dyn_close(VALUE self) {
  intptr_t fd = iodine_get_fd(self);
  sock_close(fd);
  return self;
}

/* *****************************************************************************
The Core dynamic Iodine protocol task implementation
*/

static void dyn_perform_defer(intptr_t uuid, protocol_s *protocol, void *arg) {
  (void)(uuid);
  (void)(protocol);
  RubyCaller.call((VALUE)arg, call_proc_id);
  Registry.remove((VALUE)arg);
}
static void dyn_defer_fallback(intptr_t uuid, void *arg) {
  (void)(uuid);
  Registry.remove((VALUE)arg);
};

/**
Runs the required block later (defers the blocks execution).

Unlike {Iodine#run}, the block will **not* run concurrently with any other
callback
for this object (except `ping` and `on_ready`).

Also, unlike {Iodine#run}, the block will **not** be called unless the
connection remains open at the time it's execution is scheduled.

Always returns `self`.
*/
static VALUE dyn_defer(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);
  intptr_t fd = iodine_get_fd(self);
  server_task(fd, dyn_perform_defer, (void *)block, dyn_defer_fallback);
  return self;
}

static void dyn_perform_each_task(intptr_t fd, protocol_s *protocol,
                                  void *data) {
  (void)(fd);
  RubyCaller.call2((VALUE)data, call_proc_id, 1,
                   &(dyn_prot(protocol)->handler));
}
static void dyn_finish_each_task(intptr_t fd, protocol_s *protocol,
                                 void *data) {
  (void)(protocol);
  (void)(fd);
  Registry.remove((VALUE)data);
}

void iodine_run_each(intptr_t origin, const char *service, VALUE block) {
  server_each(origin, service, dyn_perform_each_task, (void *)block,
              dyn_finish_each_task);
}

/**
Runs the required block for each dynamic protocol connection except this one.

Tasks will be performed within each connections lock, so no connection will have
more then one task being performed at the same time (similar to {#defer}).

Also, unlike {Iodine.run}, the block will **not** be called unless the
connection remains open at the time it's execution is scheduled.

Always returns `self`.
*/
static VALUE dyn_each(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);
  intptr_t fd = iodine_get_fd(self);
  server_each(fd, iodine_protocol_service, dyn_perform_each_task, (void *)block,
              dyn_finish_each_task);
  return self;
}

/**
Runs the required block for each dynamic protocol connection.

Tasks will be performed within each connections lock, so no connection will have
more then one task being performed at the same time (similar to {#defer}).

Also, unlike {Iodine.run}, the block will **not** be called unless the
connection remains open at the time it's execution is scheduled.

Always returns `self`.
*/
static VALUE dyn_class_each(VALUE self) {
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);
  server_each(-1, iodine_protocol_service, dyn_perform_each_task, (void *)block,
              dyn_finish_each_task);
  return self;
}

/* will be defined in detail later, after some more functions were written */
VALUE iodine_upgrade2basic(intptr_t fduuid, VALUE handler);

VALUE dyn_upgrade(VALUE self, VALUE handler) {
  return iodine_upgrade2basic(iodine_get_fd(self), handler);
}

/* *****************************************************************************
The Core dynamic Iodine protocol bridge
*/

/** Implement this callback to handle the event. The default implementation will
 * close the connection. */
static VALUE not_implemented_ping(VALUE self) {
  sock_close(iodine_get_fd(self));
  return Qnil;
}
/** implement this callback to handle the event. */
static VALUE not_implemented(VALUE self) {
  (void)(self);
  return Qnil;
}
/** implement this callback to handle the event. */
static VALUE not_implemented2(VALUE self, VALUE data) {
  (void)(self);
  (void)(data);
  return Qnil;
}

/**
A default on_data implementation will read up to 1Kb into a reusable buffer from
the socket and call the `on_message` callback.

It is recommended that you implement this callback if messages might require
more then 1Kb of space.
*/
static VALUE default_on_data(VALUE self) {
  VALUE buff = rb_ivar_get(self, buff_var_id);
  if (buff == Qnil) {
    rb_ivar_set(self, buff_var_id, (buff = rb_str_buf_new(1024)));
  }
  do {
    dyn_read(1, &buff, self);
    if (!RSTRING_LEN(buff))
      return Qnil;
    rb_funcall(self, on_message_func_id, 1, buff);
  } while (RSTRING_LEN(buff) == rb_str_capacity(buff));
  return Qnil;
}

/** called when a data is available, but will not run concurrently */
static void dyn_protocol_on_data(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, on_data_func_id);
}
/** called when the socket is ready to be written to. */
static void dyn_protocol_on_ready(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, on_ready_func_id);
}
/** called when the server is shutting down,
 * but before closing the connection. */
static void dyn_protocol_on_shutdown(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, on_shutdown_func_id);
}
/** called when the connection was closed, but will not run concurrently */
static void dyn_protocol_on_close(protocol_s *protocol) {
  RubyCaller.call(dyn_prot(protocol)->handler, on_close_func_id);
  Registry.remove(dyn_prot(protocol)->handler);
  free(protocol);
}
/** called when a connection's timeout was reached */
static void dyn_protocol_ping(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, ping_func_id);
}
/** Update's a connection's handler and timeout. */
static inline protocol_s *dyn_set_protocol(intptr_t fduuid, VALUE handler,
                                           uint8_t timeout) {
  Registry.add(handler);
  iodine_set_fd(handler, fduuid);
  dyn_protocol_s *protocol = malloc(sizeof(*protocol));
  if (protocol == NULL) {
    Registry.remove(handler);
    return NULL;
  }
  server_set_timeout(fduuid, timeout);
  *protocol = (dyn_protocol_s){
      .handler = handler,
      .protocol.on_data = dyn_protocol_on_data,
      .protocol.on_close = dyn_protocol_on_close,
      .protocol.on_shutdown = dyn_protocol_on_shutdown,
      .protocol.on_ready = dyn_protocol_on_ready,
      .protocol.ping = dyn_protocol_ping,
      .protocol.service = iodine_protocol_service,
  };
  RubyCaller.call(handler, on_open_func_id);
  return (protocol_s *)protocol;
}

static protocol_s *on_open_dyn_protocol(intptr_t fduuid, void *udata) {
  VALUE rb_tout = rb_ivar_get((VALUE)udata, timeout_var_id);
  uint8_t timeout = (TYPE(rb_tout) == T_FIXNUM) ? FIX2UINT(rb_tout) : 0;
  VALUE handler = RubyCaller.call((VALUE)udata, new_func_id);
  if (handler == Qnil)
    return NULL;
  return dyn_set_protocol(fduuid, handler, timeout);
}

/** called once, when Iodine starts running. */
static void on_server_start_for_handler(void *udata) {
  RubyCaller.call((VALUE)udata, on_start_func_id);
}
/** called once, when Iodine stops running. */
static void on_server_on_finish_for_handler(void *udata) {
  RubyCaller.call((VALUE)udata, on_finish_func_id);
}

void Init_DynamicProtocol(void) {
  /**
  The Protocol module is included in any object or class that handles an Iodine
  connection using a custom / dynamic protocol (not the Websockets or HTTP
  protocols that Iodine supports natively).
  */
  DynamicProtocolClass = rb_define_module_under(IodineBase, "ProtocolClass");
  rb_define_method(DynamicProtocolClass, "on_start", not_implemented, 0);
  rb_define_method(DynamicProtocolClass, "on_finish", not_implemented, 0);
  rb_define_method(DynamicProtocolClass, "each", dyn_class_each, 0);

  DynamicProtocol = rb_define_module_under(Iodine, "Protocol");
  rb_define_method(DynamicProtocol, "on_open", not_implemented, 0);
  rb_define_method(DynamicProtocol, "on_close", not_implemented, 0);
  rb_define_method(DynamicProtocol, "on_message", not_implemented2, 1);
  rb_define_method(DynamicProtocol, "on_data", default_on_data, 0);
  rb_define_method(DynamicProtocol, "on_ready", not_implemented, 0);
  rb_define_method(DynamicProtocol, "on_shutdown", not_implemented, 0);
  rb_define_method(DynamicProtocol, "ping", not_implemented_ping, 0);

  // helper methods
  rb_define_method(DynamicProtocol, "read", dyn_read, -1);
  rb_define_method(DynamicProtocol, "write", dyn_write, 1);
  rb_define_method(DynamicProtocol, "write_urgent", dyn_write_urgent, 1);
  rb_define_method(DynamicProtocol, "close", dyn_close, 0);
  rb_define_method(DynamicProtocol, "defer", dyn_defer, 0);
  rb_define_method(DynamicProtocol, "each", dyn_each, 0);
  rb_define_method(DynamicProtocol, "upgrade", dyn_upgrade, 1);
  rb_define_method(DynamicProtocol, "timeout=", dyn_set_timeout, 1);
  rb_define_method(DynamicProtocol, "timeout", dyn_get_timeout, 0);
}

/* *****************************************************************************
Iodine functions
*/

/** Sets up a listening socket. Conncetions received at the assigned port will
be handled by the assigned handler.

Multiple services (listening sockets) can be registered before starting the
Iodine event loop. */
static VALUE iodine_listen_dyn_protocol(VALUE self, VALUE port, VALUE handler) {
  // validate that the handler is a class and include the Iodine::Protocol
  if (TYPE(handler) == T_CLASS) {
    // include the Protocol module
    // // do we neet to check?
    // if (rb_mod_include_p(protocol, rDynProtocol) == Qfalse)
    rb_include_module(handler, DynamicProtocol);
    rb_extend_object(handler, DynamicProtocolClass);
  } else {
    rb_raise(rb_eTypeError, "The connection handler MUST be of type Class.");
    return Qnil;
  }
  if (TYPE(port) != T_FIXNUM && TYPE(port) != T_STRING)
    rb_raise(rb_eTypeError, "The port variable must be a Fixnum or a String.");
  if (TYPE(port) == T_FIXNUM)
    port = rb_funcall2(port, to_s_method_id, 0, NULL);
  // listen
  server_listen(.port = StringValueCStr(port), .udata = (void *)handler,
                .on_open = on_open_dyn_protocol,
                .on_start = on_server_start_for_handler,
                .on_finish = on_server_on_finish_for_handler);
  return self;
}

VALUE iodine_upgrade2basic(intptr_t fduuid, VALUE handler) {
  uint8_t timeout;
  if (TYPE(handler) == T_CLASS) {
    // get the timeout
    VALUE rb_tout = rb_ivar_get(handler, timeout_var_id);
    timeout = (TYPE(rb_tout) == T_FIXNUM) ? FIX2UINT(rb_tout) : 0;
    // include the Protocol module
    // // do we neet to check?
    // if (rb_mod_include_p(protocol, rDynProtocol) == Qfalse)
    rb_include_module(handler, DynamicProtocol);
    handler = RubyCaller.call(handler, new_func_id);
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    // // do we neet to check?
    // if (rb_mod_include_p(p_class, rDynProtocol) == Qfalse)
    rb_include_module(p_class, DynamicProtocol);
    // get the timeout
    VALUE rb_tout = rb_ivar_get(p_class, timeout_var_id);
    if (rb_tout == Qnil)
      rb_tout = rb_ivar_get(handler, timeout_var_id);
    timeout = (TYPE(rb_tout) == T_FIXNUM) ? FIX2UINT(rb_tout) : 0;
  }
  protocol_s *protocol = dyn_set_protocol(fduuid, handler, timeout);
  if (protocol) {
    if (server_switch_protocol(fduuid, protocol))
      dyn_protocol_on_close(protocol);
    return handler;
  }
  return Qfalse;
}

/* *****************************************************************************
Iodine Task Management
*/

static void iodine_run_once(void *block) {
  RubyCaller.call((VALUE)block, call_proc_id);
  Registry.remove((VALUE)block);
}

static void iodine_run_always(void *block) {
  RubyCaller.call((VALUE)block, call_proc_id);
}

/**
Runs the required block later. The block might run concurrently with the
existing code (depending on the amount and availability of worker threads).

Returns the block object. The block will run only while Iodine is running (run
will be delayed until Iodine.start is called, unless Iodine's event loop is
active).
*/
static VALUE iodine_run_async(VALUE self) {
  (void)(self);
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);
  if (async_run(iodine_run_once, (void *)block)) {
    server_run_after(1, iodine_run_once, (void *)block);
    ;
  }
  return block;
}

/**
Runs the required block after the specified number of milliseconds have passed.
Time is counted only once Iodine started running (using {Iodine.start}).

Always returns a copy of the block object.
*/
static VALUE iodine_run_after(VALUE self, VALUE milliseconds) {
  (void)(self);
  if (TYPE(milliseconds) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "milliseconds must be a number");
    return Qnil;
  }
  size_t milli = FIX2UINT(milliseconds);
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);
  server_run_after(milli, iodine_run_once, (void *)block);
  return block;
}
/**
Runs the required block after the specified number of milliseconds have passed.
Time is counted only once Iodine started running (using {Iodine.start}).

Accepts:

milliseconds:: the number of milliseconds between event repetitions.

repetitions:: the number of event repetitions. Defaults to 0 (never ending).

block:: (required) a block is required, as otherwise there is nothing to
perform.

The event will repeat itself until the number of repetitions had been delpeted.

Always returns a copy of the block object.
*/
static VALUE iodine_run_every(int argc, VALUE *argv, VALUE self) {
  (void)(self);
  VALUE milliseconds, repetitions, block;

  rb_scan_args(argc, argv, "11&", &milliseconds, &repetitions, &block);

  if (TYPE(milliseconds) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "milliseconds must be a number.");
    return Qnil;
  }
  if (repetitions != Qnil && TYPE(repetitions) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "repetitions must be a number or `nil`.");
    return Qnil;
  }

  size_t milli = FIX2UINT(milliseconds);
  size_t repeat = (repetitions == Qnil) ? 0 : FIX2UINT(repetitions);
  // requires a block to be passed
  rb_need_block();
  Registry.add(block);
  server_run_every(milli, repeat, iodine_run_always, (void *)block,
                   (void (*)(void *))Registry.remove);
  return block;
}

static VALUE iodine_count(VALUE self) {
  (void)(self);
  return ULONG2NUM(server_count(NULL));
}
/* *****************************************************************************
Running the server
*/

static void *srv_start_no_gvl(void *_) {
  (void)(_);
  // collect requested settings
  VALUE rb_th_i = rb_iv_get(Iodine, "@threads");
  VALUE rb_pr_i = rb_iv_get(Iodine, "@processes");
  ssize_t threads = (TYPE(rb_th_i) == T_FIXNUM) ? FIX2LONG(rb_th_i) : 0;
  ssize_t processes = (TYPE(rb_pr_i) == T_FIXNUM) ? FIX2LONG(rb_pr_i) : 0;
// print a warnning if settings are sub optimal
#ifdef _SC_NPROCESSORS_ONLN
  size_t cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
  if (processes <= 0)
    processes = (cpu_count >> 1) ? (cpu_count >> 1) : 1;
  if (threads <= 0)
    threads = (cpu_count >> 1) ? (cpu_count >> 1) : 1;

  if (cpu_count > 0 &&
      ((processes << 1) < cpu_count || processes > (cpu_count << 1)))
    fprintf(
        stderr, "* Performance warnning:\n"
                "  - This computer has %lu CPUs available and you'll be "
                "utilizing %lu processes.\n  - %s\n"
                "  - Use the command line option: `-w %lu`\n"
                "  - Or, within Ruby: `Iodine.processes = %lu`\n",
        cpu_count, (processes ? processes : 1),
        (processes < cpu_count
             ? "Some CPUs won't be utilized, inhibiting performance."
             : "This causes excessive context switches, wasting resources."),
        cpu_count, cpu_count);
#else
  if (processes <= 0)
    processes = 1;
  if (threads <= 0)
    threads = 1;
#endif
  server_run(.threads = threads, .processes = processes);
  return NULL;
}

static void unblck(void *_) {
  (void)(_);
  server_stop();
}
/**
Starts the Iodine event loop. This will hang the thread until an interrupt
(`^C`) signal is received.

Returns the Iodine module.
*/
static VALUE iodine_start(VALUE self) {
  if (iodine_http_review() == -1) {
    perror("Iodine couldn't start HTTP service... port busy? ");
    return Qnil;
  }
  rb_thread_call_without_gvl2(srv_start_no_gvl, (void *)self, unblck, NULL);

  return self;
}

/* *****************************************************************************
Initializing the library
*/

////////////////////////////////////////////////////////////////////////
// Ruby loads the library and invokes the Init_<lib_name> function...
//
// Here we connect all the C code to the Ruby interface, completing the bridge
// between Lib-Server and Ruby.
void Init_iodine(void) {
  // initialize globally used IDs, for faster access to the Ruby layer.
  call_proc_id = rb_intern("call");
  new_func_id = rb_intern("new");
  on_start_func_id = rb_intern("on_start");
  on_finish_func_id = rb_intern("on_finish");
  on_open_func_id = rb_intern("on_open");
  on_message_func_id = rb_intern("on_message");
  on_data_func_id = rb_intern("on_data");
  on_shutdown_func_id = rb_intern("on_shutdown");
  on_close_func_id = rb_intern("on_close");
  on_ready_func_id = rb_intern("on_ready");
  ping_func_id = rb_intern("ping");
  buff_var_id = rb_intern("scrtbuffer");
  fd_var_id = rb_intern("scrtfd");
  timeout_var_id = rb_intern("@timeout");
  to_s_method_id = rb_intern("to_s");

  BinaryEncodingIndex = rb_enc_find_index("binary"); // sets encoding for data
  UTF8EncodingIndex = rb_enc_find_index("UTF-8");    // sets encoding for data
  BinaryEncoding = rb_enc_find("binary");            // sets encoding for data
  UTF8Encoding = rb_enc_find("UTF-8");               // sets encoding for data

  // The core Iodine module wraps libserver functionality and little more.
  Iodine = rb_define_module("Iodine");

  // get-set version
  {
    Iodine_Version = rb_const_get(Iodine, rb_intern("VERSION"));
    if (Iodine_Version == Qnil)
      Iodine_Version_Str = "0.2.0";
    else
      Iodine_Version_Str = StringValueCStr(Iodine_Version);
  }
  // the Iodine singleton functions
  rb_define_module_function(Iodine, "listen", iodine_listen_dyn_protocol, 2);
  rb_define_module_function(Iodine, "start", iodine_start, 0);
  rb_define_module_function(Iodine, "count", iodine_count, 0);
  rb_define_module_function(Iodine, "run", iodine_run_async, 0);
  rb_define_module_function(Iodine, "run_after", iodine_run_after, 1);
  rb_define_module_function(Iodine, "run_every", iodine_run_every, -1);

  // Every Protocol (and Server?) instance will hold a reference to the server
  // define the Server Ruby class.
  IodineBase = rb_define_module_under(Iodine, "Base");

  // Initialize the registry under the Iodine core
  Registry.init(Iodine);
  // Initialize the Dynamic Protocol
  Init_DynamicProtocol();

  // initialize the Http server
  // must be done only after all the globals, including BinaryEncoding, are set
  Init_iodine_http();
}
