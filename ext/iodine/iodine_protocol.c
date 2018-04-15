/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_protocol.h"
#include "iodine_pubsub.h"

#include "fio_llist.h"
#include "fiobj4sock.h"
#include "pubsub.h"

#include <ruby/io.h>

static ID iodine_close_func_id;

/* *****************************************************************************
The protocol object
***************************************************************************** */

typedef struct {
  protocol_s protocol;
  VALUE handler;
  fio_ls_s subscriptions;
} iodine_protocol_s;

VALUE IodineProtocol;

static char *iodine_protocol_service = "Iodine Custom Protocol";

/* *****************************************************************************
Internal helpers
***************************************************************************** */

static void iodine_clear_task(intptr_t origin, void *block_) {
  Registry.remove((VALUE)block_);
  (void)origin;
}

// static void iodine_perform_task(intptr_t uuid, protocol_s *pr, void *block_)
// {
//   if (pr->service == iodine_protocol_service) {
//     RubyCaller.call2((VALUE)block_, iodine_call_proc_id, 1, (VALUE *)(pr +
//     1));
//   }
//   (void)uuid;
// }

static void iodine_perform_task_and_free(intptr_t uuid, protocol_s *pr,
                                         void *block_) {
  if (pr->service == iodine_protocol_service) {
    RubyCaller.call2((VALUE)block_, iodine_call_proc_id, 1, (VALUE *)(pr + 1));
  }
  Registry.remove((VALUE)block_);
  (void)uuid;
}

static void remove_from_registry(intptr_t uuid, void *val) {
  Registry.remove((VALUE)val);
  (void)uuid;
}
/* *****************************************************************************
Function placeholders
***************************************************************************** */

/** Override this callback to handle the event. The default implementation will
 * close the connection. */
static VALUE not_implemented_ping(VALUE self) {
  sock_close(iodine_get_fd(self));
  return Qnil;
}
/** Override this callback to handle the event. */
static VALUE not_implemented(VALUE self) {
  (void)(self);
  return Qnil;
}

/** Override this callback to handle the event. */
static VALUE not_implemented2(VALUE self, VALUE data) {
  (void)(self);
  (void)(data);
  return Qnil;
}

static VALUE dyn_read(int argc, VALUE *argv, VALUE self);
/**
A default on_data implementation will read up to 1Kb into a reusable buffer from
the socket and call the `on_message` callback.

It is recommended that you implement this callback if messages might require
more then 1Kb of space.
*/
static VALUE default_on_data(VALUE self) {
  VALUE buff = rb_ivar_get(self, iodine_buff_var_id);
  if (buff == Qnil) {
    rb_ivar_set(self, iodine_buff_var_id, (buff = rb_str_buf_new(1024)));
  }
  do {
    dyn_read(1, &buff, self);
    if (!RSTRING_LEN(buff))
      return Qnil;
    rb_funcall(self, iodine_on_message_func_id, 1, buff);
  } while (RSTRING_LEN(buff) == (ssize_t)rb_str_capacity(buff));
  return Qnil;
}

/* *****************************************************************************
Pub/Sub functions
***************************************************************************** */

// clang-format off
/**
Subscribes the connection to a Pub/Sub channel.

Since this connection's data packaging is unknown, a block (or handler) is required to handle  pub/sub events.

The method accepts 1-2 arguments and an optional block. These are all valid ways
to call the method:

      subscribe("my_stream") {|from, msg| p msg }
      subscribe("my_stream", match: :redis) {|from, msg| p msg }
      subscribe(to: "my_stream")  {|from, msg| p msg }
      subscribe to: "my_stream", match: :redis, handler: MyProc

The first argument must be either a String or a Hash.

The second, optional, argument must be a Hash (if given).

The options Hash supports the following possible keys (other keys are ignored, all keys are Symbols):

:match :: The channel / subject name matching type to be used. Valid value is: `:redis`. Future versions hope to support `:nats` and `:rabbit` patern matching as well.

:to :: The channel / subject to subscribe to.

:handler :: and Proc like object, must answer to `call(from, msg)`.

Returns an {Iodine::PubSub::Subscription} object that answers to:

close :: closes the connection.
to_s :: returns the subscription's target (stream / channel / subject).
==(str) :: returns true if the string is an exact match for the target (even if the target itself is a pattern).

*/
static VALUE iodine_proto_subscribe(int argc, VALUE *argv, VALUE self) {
  // clang-format on
  intptr_t uuid = iodine_get_fd(self);
  if (!uuid || (VALUE)uuid == Qnil || uuid < 0)
    return Qfalse;
  VALUE sub = iodine_subscribe(argc, argv, NULL, IODINE_PUBSUB_GLOBAL);
  if (sub == Qnil || sub == Qfalse)
    return Qfalse;
  Registry.add(sub);

  iodine_protocol_s *pr = iodine_get_cdata(self);

  fio_ls_push(&pr->subscriptions, (void *)sub);
  return sub;
}

/* *****************************************************************************
Published functions
***************************************************************************** */

/**
Reads up to `n` bytes from the network connection.
The number of bytes to be read (n) is:
- the number of bytes set in the optional `buffer_or_length` argument.
- the String capacity (not length) of the String passed as the optional
  `buffer_or_length` argument.
- 1024 Bytes (1Kb) if the optional `buffer_or_length` is either missing or
  contains a String with a capacity less then 1Kb.
Returns a String (either the same one used as the buffer or a new one) on a
successful read.
Returns `nil` if no data was available.
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
  rb_enc_associate_index(str, IodineBinaryEncodingIndex);
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
  Check_Type(data, T_STRING);
  intptr_t fd = iodine_get_fd(self);
  if (sock_write(fd, RSTRING_PTR(data), RSTRING_LEN(data))) {
    return Qfalse;
  }
  return self;
}

/**
Moves a String to iodine's socket's buffer. This is a zero-copy write and
requires that the string remain unchanged during the process.

For example, Strings received by `on_message` can't be used, because they use a
recyclable buffer and they will be destroyed once `on_message` returns.
*/
static VALUE dyn_write_move(VALUE self, VALUE data) {
  Check_Type(data, T_STRING);
  Registry.add(data);
  intptr_t fd = iodine_get_fd(self);
  if (sock_write2(.uuid = fd, .buffer = RSTRING_PTR(data),
                  .length = RSTRING_LEN(data),
                  .dealloc = (void (*)(void *))Registry.remove))
    return Qfalse;
  return self;
}

/**
Writes data to the connection. The data will be sent as soon as possible without
fragmantation of previously scheduled data.

Returns `false` on error and `self` on success.
*/
static VALUE dyn_write_urgent(VALUE self, VALUE data) {
  Check_Type(data, T_STRING);
  intptr_t fd = iodine_get_fd(self);
  Registry.add(data);
  if (sock_write(fd, RSTRING_PTR(data), RSTRING_LEN(data))) {
    return Qfalse;
  }
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
  facil_set_timeout(fd, tout);
  return self;
}

/**
Returns the connection's timeout.
*/
static VALUE dyn_get_timeout(VALUE self) {
  intptr_t fd = iodine_get_fd(self);
  uint8_t tout = facil_get_timeout(fd);
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

/**
Returns a connection's localized ID which is valid for *this process* (not a
machine or internet unique value).

Once the connection is closed and the `on_close` callback was called, this
method returns `nil`.

This can be used together with a true process wide UUID to uniquely identify a
connection across the internet.
*/
static VALUE dyn_uuid(VALUE self) {
  intptr_t uuid = iodine_get_fd(self);
  if (!uuid || uuid == -1)
    return Qnil;
  return LONG2FIX(uuid);
}

/**
Returns true if the connection is open and false if closed.
*/
static VALUE dyn_is_open(VALUE self) {
  intptr_t uuid = iodine_get_fd(self);
  if (uuid && sock_isvalid(uuid))
    return Qtrue;
  return Qfalse;
}

/**
Schedules a block to execute (defers the blocks execution).

When this function is called by a Protocol instance, a lock on the connection
will be used to prevent multiple tasks / callbacks from running concurrently.
i.e.

      defer { write "this will run in a lock" }

Otherwise, the deferred task will run acconrding to the requested concurrency
model.

      Iodine.defer { puts "this will run concurrently" }
      Iodine.run { puts "this will run concurrently" }

Tasks scheduled before calling {Iodine.start} will run once for every process.

Returns the block given (or `false`).

**Notice**: There's a possibility that the rask will never be called if it was
associated with a specific connection (the method was called as an instance
method) and the connection was closed before the deferred task was performed.
*/
static VALUE dyn_defer(int argc, VALUE *argv, VALUE self) {
  rb_need_block();
  intptr_t fd;
  // check arguments.
  if (argc > 1)
    rb_raise(rb_eArgError, "this function expects no more then 1 (optional) "
                           "argument.");
  else if (argc == 1) {
    Check_Type(*argv, T_FIXNUM);
    fd = FIX2LONG(*argv);
  } else
    fd = iodine_get_fd(self);

  if (!sock_isvalid(fd))
    return Qfalse;

  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  Registry.add(block);
  facil_defer(.uuid = fd, .task = iodine_perform_task_and_free,
              .type = FIO_PR_LOCK_TASK, .arg = (void *)block,
              .fallback = iodine_clear_task);
  return block;
}

/* *****************************************************************************
Connection management
***************************************************************************** */
#define dyn_prot(protocol) ((iodine_protocol_s *)(protocol))

/** called when a data is available, but will not run concurrently */
static void dyn_protocol_on_data(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, iodine_on_data_func_id);
}
/** called when the socket is ready to be written to. */
static void dyn_protocol_on_ready(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, iodine_on_ready_func_id);
}
/** called when the server is shutting down,
 * but before closing the connection. */
static void dyn_protocol_on_shutdown(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, iodine_on_shutdown_func_id);
}

static void *clear_subscriptions_inGVL(void *pr_) {
  iodine_protocol_s *pr = pr_;
  while (fio_ls_any(&pr->subscriptions)) {
    RubyCaller.call((VALUE)fio_ls_pop(&pr->subscriptions),
                    iodine_close_func_id);
  }
  return NULL;
}

/** called when the connection was closed, but will not run concurrently */
static void dyn_protocol_on_close(intptr_t uuid, protocol_s *protocol) {
  RubyCaller.call(dyn_prot(protocol)->handler, iodine_on_close_func_id);
  iodine_set_fd(dyn_prot(protocol)->handler, 0);
  iodine_set_cdata(dyn_prot(protocol)->handler, NULL);
  Registry.remove(dyn_prot(protocol)->handler);
  RubyCaller.call_c(clear_subscriptions_inGVL, protocol);
  free(protocol);
  (void)uuid;
}
/** called when a connection's timeout was reached */
static void dyn_protocol_ping(intptr_t fduuid, protocol_s *protocol) {
  (void)(fduuid);
  RubyCaller.call(dyn_prot(protocol)->handler, iodine_ping_func_id);
}

/* *****************************************************************************
Connection management API
*****************************************************************************
*/

/** Update's a connection's handler and timeout. */
static inline protocol_s *dyn_set_protocol(intptr_t fduuid, VALUE handler,
                                           uint8_t timeout) {
  Registry.add(handler);
  iodine_protocol_s *protocol = malloc(sizeof(*protocol));
  if (protocol == NULL) {
    Registry.remove(handler);
    return NULL;
  }
  facil_set_timeout(fduuid, timeout);
  *protocol = (iodine_protocol_s){
      .protocol.on_data = dyn_protocol_on_data,
      .protocol.on_close = dyn_protocol_on_close,
      .protocol.on_shutdown = dyn_protocol_on_shutdown,
      .protocol.on_ready = dyn_protocol_on_ready,
      .protocol.ping = dyn_protocol_ping,
      .protocol.service = iodine_protocol_service,
      .handler = handler,
      .subscriptions = FIO_LS_INIT(protocol->subscriptions),
  };
  iodine_set_fd(handler, fduuid);
  iodine_set_cdata(handler, protocol);
  RubyCaller.call(handler, iodine_on_open_func_id);
  return &protocol->protocol;
}

static void on_open_dyn_protocol(intptr_t fduuid, void *udata) {
  VALUE rb_tout = rb_ivar_get((VALUE)udata, iodine_timeout_var_id);
  uint8_t timeout = (TYPE(rb_tout) == T_FIXNUM) ? FIX2UINT(rb_tout) : 0;
  VALUE handler = RubyCaller.call((VALUE)udata, iodine_new_func_id);
  if (handler == Qnil) {
    sock_close(fduuid);
    return;
  }
  facil_attach(fduuid, dyn_set_protocol(fduuid, handler, timeout));
}

/** Sets up a listening socket. Conncetions received at the assigned port will
be handled by the assigned handler.

Multiple services (listening sockets) can be registered before starting the
Iodine event loop. */
static VALUE iodine_listen(VALUE self, VALUE port, VALUE handler) {
  // validate that the handler is a class and include the Iodine::Protocol
  if (TYPE(handler) == T_CLASS) {
    // include the Protocol module
    // // do we neet to check?
    // if (rb_mod_include_p(protocol, rDynProtocol) == Qfalse)
    rb_include_module(handler, IodineProtocol);
    rb_extend_object(handler, IodineProtocol);
  } else {
    rb_raise(rb_eTypeError, "The connection handler MUST be of type Class.");
    return Qnil;
  }
  if (TYPE(port) != T_FIXNUM && TYPE(port) != T_STRING)
    rb_raise(rb_eTypeError, "The port variable must be a Fixnum or a String.");
  if (TYPE(port) == T_FIXNUM)
    port = rb_funcall2(port, iodine_to_s_method_id, 0, NULL);
  rb_ivar_set(self, rb_intern("_port"), port);
  // listen
  if (facil_listen(.port = StringValueCStr(port), .udata = (void *)handler,
                   .on_open = on_open_dyn_protocol) == -1)
    return Qnil;
  return self;
}

VALUE dyn_switch_prot(VALUE self, VALUE handler) {
  uint8_t timeout;
  intptr_t fd = iodine_get_fd(self);
  if (TYPE(handler) == T_CLASS) {
    // get the timeout
    VALUE rb_tout = rb_ivar_get(handler, iodine_timeout_var_id);
    timeout = (TYPE(rb_tout) == T_FIXNUM) ? FIX2UINT(rb_tout) : 0;
    // include the Protocol module, preventing coder errors
    rb_include_module(handler, IodineProtocol);
    handler = RubyCaller.call(handler, iodine_new_func_id);
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    // include the Protocol module, preventing coder errors
    rb_include_module(p_class, IodineProtocol);
    // get the timeout
    VALUE rb_tout = rb_ivar_get(p_class, iodine_timeout_var_id);
    if (rb_tout == Qnil)
      rb_tout = rb_ivar_get(handler, iodine_timeout_var_id);
    timeout = (TYPE(rb_tout) == T_FIXNUM) ? FIX2UINT(rb_tout) : 0;
  }
  if (facil_attach(fd, dyn_set_protocol(fd, handler, timeout)))
    return Qnil;
  return handler;
}

static void on_open_dyn_protocol_instance(intptr_t fduuid, void *udata) {
  VALUE rb_tout = rb_ivar_get((VALUE)udata, iodine_timeout_var_id);
  uint8_t timeout = (TYPE(rb_tout) == T_FIXNUM) ? FIX2UINT(rb_tout) : 0;
  protocol_s *pr = dyn_set_protocol(fduuid, (VALUE)udata, timeout);
  Registry.remove((VALUE)udata);
  facil_attach(fduuid, pr);
}

/**
Connects (as a TCP/IP client) to a remote TCP/IP server.

i.e.

      Iodine.connect "example.com", 5000, MyProtocolClass.new

*/
static VALUE iodine_connect(VALUE self, VALUE address, VALUE port,
                            VALUE handler) {
  if (TYPE(handler) == T_CLASS || TYPE(handler) == T_MODULE) {
    // include the Protocol module, preventing coder errors
    rb_include_module(handler, IodineProtocol);
    handler = RubyCaller.call(handler, iodine_new_func_id);
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    // include the Protocol module, preventing coder errors
    rb_include_module(p_class, IodineProtocol);
  }
  if (TYPE(port) != T_FIXNUM && TYPE(port) != T_STRING)
    rb_raise(rb_eTypeError, "The port variable must be a Fixnum or a String.");
  Registry.add(handler);
  if (TYPE(port) == T_FIXNUM)
    port = rb_funcall2(port, iodine_to_s_method_id, 0, NULL);
  // connect
  intptr_t uuid = facil_connect(.port = StringValueCStr(port),
                                .address = StringValueCStr(address),
                                .udata = (void *)handler,
                                .on_connect = on_open_dyn_protocol_instance,
                                .on_fail = remove_from_registry);
  if (uuid == -1)
    return Qnil;
  iodine_set_fd(handler, uuid);
  return handler;
  (void)self;
}

/**
Attaches an existing file descriptor (`fd`) (i.e., a pipe, a unix socket,
etc') as if it were a regular connection.

i.e.

      Iodine.attach my_io_obj.to_i, MyProtocolClass.new

*/
static VALUE iodine_attach_fd(VALUE self, VALUE rbfd, VALUE handler) {
  Check_Type(rbfd, T_FIXNUM);
  if (handler == Qnil || handler == Qfalse)
    return Qfalse;
  intptr_t uuid = FIX2INT(rbfd);
  if (!uuid || uuid == -1)
    return Qfalse;
  /* make sure the uuid is connected to the sock library */
  if (sock_fd2uuid(uuid) == -1)
    sock_open(uuid);
  if (TYPE(handler) == T_CLASS) {
    // include the Protocol module, preventing coder errors
    rb_include_module(handler, IodineProtocol);
    handler = RubyCaller.call(handler, iodine_new_func_id);
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    // include the Protocol module, preventing coder errors
    rb_include_module(p_class, IodineProtocol);
  }
  Registry.add(handler);
  on_open_dyn_protocol_instance(uuid, (void *)handler);
  return self;
}
/**
Attaches an existing IO object (i.e., a pipe, a unix socket, etc') as if it
were a regular connection.

i.e.

      Iodine.attach my_io_obj, MyProtocolClass.new

*/
static VALUE iodine_attach_io(VALUE self, VALUE io, VALUE handler) {
  return iodine_attach_fd(self, RubyCaller.call(io, iodine_to_i_func_id),
                          handler);
}
/* *****************************************************************************
Library Initialization
*****************************************************************************
*/

////////////////////////////////////////////////////////////////////////
// Ruby loads the library and invokes the Init_<lib_name> function...
//
// Here we connect all the C code to the Ruby interface, completing the bridge
// between Lib-Server and Ruby.
void Iodine_init_protocol(void) {

  iodine_close_func_id = rb_intern("close");

  /* add Iodine module functions */
  rb_define_module_function(Iodine, "listen", iodine_listen, 2);
  rb_define_module_function(Iodine, "connect", iodine_connect, 3);
  rb_define_module_function(Iodine, "attach_io", iodine_attach_io, 2);
  rb_define_module_function(Iodine, "attach_fd", iodine_attach_fd, 2);

  /* Create the `Protocol` module and set stub functions */
  IodineProtocol = rb_define_module_under(Iodine, "Protocol");
  rb_define_method(IodineProtocol, "on_open", not_implemented, 0);
  rb_define_method(IodineProtocol, "on_close", not_implemented, 0);
  rb_define_method(IodineProtocol, "on_message", not_implemented2, 1);
  rb_define_method(IodineProtocol, "on_data", default_on_data, 0);
  rb_define_method(IodineProtocol, "on_ready", not_implemented, 0);
  rb_define_method(IodineProtocol, "on_shutdown", not_implemented, 0);
  rb_define_method(IodineProtocol, "ping", not_implemented_ping, 0);

  /* Add module functions */
  rb_define_singleton_method(IodineProtocol, "defer", dyn_defer, -1);

  /* Add module instance methods */
  rb_define_method(IodineProtocol, "open?", dyn_is_open, 0);
  rb_define_method(IodineProtocol, "conn_id", dyn_uuid, 0);
  rb_define_method(IodineProtocol, "read", dyn_read, -1);
  rb_define_method(IodineProtocol, "write", dyn_write, 1);
  rb_define_method(IodineProtocol, "write!", dyn_write_move, 1);
  rb_define_method(IodineProtocol, "write_urgent", dyn_write_urgent, 1);
  rb_define_method(IodineProtocol, "close", dyn_close, 0);
  rb_define_method(IodineProtocol, "defer", dyn_defer, -1);
  rb_define_method(IodineProtocol, "switch_protocol", dyn_switch_prot, 1);
  rb_define_method(IodineProtocol, "timeout=", dyn_set_timeout, 1);
  rb_define_method(IodineProtocol, "timeout", dyn_get_timeout, 0);
  rb_define_method(IodineProtocol, "subscribe", iodine_proto_subscribe, -1);
  rb_define_method(IodineProtocol, "publish", iodine_publish, -1);
}
