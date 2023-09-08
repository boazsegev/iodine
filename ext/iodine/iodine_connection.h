#include "iodine.h"

/* *****************************************************************************
Ruby Object.
***************************************************************************** */

FIO_LEAK_COUNTER_DEF(iodine_connection)

typedef struct iodine_connection_s {
  fio_s *io;
  fio_http_s *http;
  VALUE handler;
  VALUE env;
} iodine_connection_s;

static void iodine_connection_free(void *ptr_) {
  iodine_connection_s *c = (iodine_connection_s *)ptr_;
  if (!c)
    return;
  FIO_LEAK_COUNTER_ON_FREE(iodine_connection);
  if (c->env)
    STORE.release(c->env);
}

static VALUE iodine_connection_alloc(VALUE klass) {
  iodine_connection_s *wrapper;
  VALUE o = Data_Make_Struct(klass,
                             iodine_connection_s,
                             NULL,
                             iodine_connection_free,
                             wrapper);
  *wrapper = (iodine_connection_s){NULL};
  FIO_LEAK_COUNTER_ON_ALLOC(iodine_connection);
  return o;
}

static iodine_connection_s *iodine_connection_get(VALUE self) {
  iodine_connection_s *c;
  Data_Get_Struct(self, iodine_connection_s, c);
  return c;
}

/* *****************************************************************************
Subscription Helpers
***************************************************************************** */

static void iodine_connection_on_message(fio_msg_s *m) {}

FIO_IFUNC VALUE iodine_connection_subscribe_internal(fio_s *io,
                                                     int argc,
                                                     VALUE *argv) {
  VALUE name = Qnil;
  VALUE filter = Qnil;
  VALUE proc = Qnil;
  long long filter_i = 0;
  fio_buf_info_s channel = FIO_BUF_INFO2(NULL, 0);
  fio_rb_multi_arg(argc,
                   argv,
                   FIO_RB_ARG(name, 0, "channel", Qnil, 0),
                   FIO_RB_ARG(filter, 0, "filter", Qnil, 0),
                   FIO_RB_ARG(proc, 0, "proc", Qnil, 0));
  if (RB_TYPE_P(name, RUBY_T_SYMBOL))
    name = rb_sym_to_s(name);
  if (name != Qnil) {
    rb_check_type(name, RUBY_T_STRING);
    channel = FIO_BUF_INFO2(RSTRING_PTR(name), (size_t)RSTRING_LEN(name));
  }
  if (filter != Qnil) {
    rb_check_type(filter, RUBY_T_FIXNUM);
    filter_i = RB_NUM2LL(filter);
    if ((size_t)filter_i & (~(size_t)0xFFFF))
      rb_raise(rb_eRangeError, "filter out of range (%lld > 0xFFFF)", filter_i);
  }
  if (rb_block_given_p() && proc == Qnil)
    proc = rb_block_proc();
  else if (proc != Qnil && !rb_respond_to(proc, rb_intern2("call", 4)))
    rb_raise(rb_eArgError, "a callback object MUST respond to `call`");
  if (!io && proc == Qnil)
    rb_raise(rb_eArgError,
             "Global subscriptions require a callback (proc/block) object!");
  STORE.hold(proc);
  fio_subscribe(.io = io,
                .filter = (int16_t)filter_i,
                .channel = channel,
                .udata = (void *)proc,
                .on_message =
                    ((proc == Qnil) ? NULL : iodine_connection_on_message),
                .on_unsubscribe = (void (*)(void *))STORE.release);
}

/* *****************************************************************************
Ruby Public API.
***************************************************************************** */

/** Initializes a Connection object. */
static VALUE iodine_connection_initialize(int argc, VALUE *argv, VALUE self) {
  rb_raise(rb_eException, "Iodine::Connection.new shouldn't be called!");
  return self;
}

/** Returns the connection's env (if it originated from an HTTP request). */
static VALUE iodine_connection_env(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c)
    return Qnil;
  if (c->env)
    return c->env;
  if (!c->http)
    return Qnil;
  /* TODO: convert HTTP to Rack env object. */
  return Qnil;
}

/** Returns the client's current callback object. */
static VALUE iodine_connection_handler(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (c && c->handler)
    return c->handler;
  return Qnil;
}

/**
 * Sets the client's callback object, so future events will use the new object's
 * callbacks.
 */
static VALUE iodine_connection_handler_set(VALUE self, VALUE handler) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c)
    return Qnil;
  STORE.release(c->handler);
  STORE.hold(handler);
  c->handler = handler;
  return Qnil;
}

/** Returns true if the connection appears to be open (no known issues). */
static VALUE iodine_connection_is_open(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (c && c->io)
    return fio_srv_is_open(c->io);
  return Qfalse;
}

/**
 * Returns the number of bytes that need to be sent before the next `on_drained`
 * callback is called.
 */
static VALUE iodine_connection_pending(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (c && c->io)
    return RB_SIZE2NUM(((size_t)fio_stream_length(&c->io->stream)));
  return Qfalse;
}

/** Schedules the connection to be closed. */
static VALUE iodine_connection_close(VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (c) {
    if (c->http)
      fio_http_close(c->http);
    else if (c->io)
      fio_close(c->io);
  }
  return self;
}

/**
 * Always returns true, since Iodine connections support the pub/sub extension.
 */
static VALUE iodine_connection_has_pubsub(VALUE self) { return Qtrue; }

/** Writes data to the connection asynchronously. */
static VALUE iodine_connection_write(VALUE self, VALUE data) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c || (!c->http && !c->io))
    return Qnil;
  fio_str_info_s to_write;
  unsigned to_copy = 1;
  void (*dealloc)(void *) = NULL;
  if (RB_TYPE_P(data, RUBY_T_SYMBOL))
    data = rb_sym_to_s(data);
  if (RB_TYPE_P(data, RUBY_T_STRING)) {
    to_write = FIO_STR_INFO2(RSTRING_PTR(data), (size_t)RSTRING_LEN(data));
  } else {
    dealloc = (void (*)(void *))fio_bstr_free;
    to_copy = 0;
    to_write = fio_bstr_info(iodine_json_stringify2bstr(NULL, data));
  }
  if (c->http)
    fio_http_write(c->http,
                   .buf = to_write.buf,
                   .len = to_write.len,
                   .dealloc = dealloc,
                   .copy = to_copy);
  else if (c->io)
    fio_write2(c->io,
               .buf = to_write.buf,
               .len = to_write.len,
               .dealloc = dealloc,
               .copy = to_copy);
  return Qtrue;
}

/**
 * Subscribes to a combination of a channel (String) and filter (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the subscription's channel name (String).
 * - `filter`:  the subscription's filter (Number < 32,767).
 * - `proc`:    an optional object that answers to `call` and accepts a single
 *              argument (the message structure).
 *
 * i.e.:
 *
 *     subscribe("name")
 *     subscribe(channel: "name")
 *     # or with filter
 *     subscribe("name", 1)
 *     subscribe(channel: "name", filter: 1)
 *     # or only a filter
 *     subscribe(nil, 1)
 *     subscribe(filter: 1)
 *     # with / without a proc
 *     subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil; }
 *     subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
 *
 */
static VALUE iodine_connection_subscribe(int argc, VALUE *argv, VALUE self) {
  iodine_connection_s *c = iodine_connection_get(self);
  if (!c || (!c->http && !c->io))
    return Qnil;
  if (c->io)
    iodine_connection_subscribe_internal(c->io, argc, argv);
  else
    iodine_connection_subscribe_internal(fio_http_io(c->http), argc, argv);
  return self;
}

/**
 * Subscribes to a combination of a channel (String) and filter (number).
 *
 * Accepts the following (possibly named) arguments and an optional block:
 *
 * - `channel`: the subscription's channel name (String).
 * - `filter`:  the subscription's filter (Number < 32,767).
 * - `proc`:    an optional object that answers to `call` and accepts a single
 *              argument (the message structure).
 *
 * Either a `proc` or a `block` MUST be provided for global subscriptions.
 *
 * i.e.:
 *
 *     Iodine.subscribe("name")
 *     Iodine.subscribe(channel: "name")
 *     # or with filter
 *     Iodine.subscribe("name", 1)
 *     Iodine.subscribe(channel: "name", filter: 1)
 *     # or only a filter
 *     Iodine.subscribe(nil, 1)
 *     Iodine.subscribe(filter: 1)
 *     # with / without a proc
 *     Iodine.subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil;}
 *     Iodine.subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
 *
 */
static VALUE iodine_connection_subscribe_klass(int argc,
                                               VALUE *argv,
                                               VALUE self) {
  iodine_connection_subscribe_internal(NULL, argc, argv);
  return self;
}

/* *****************************************************************************
Initialize Connection Class
***************************************************************************** */

// clang-format off
/**
module MyConnectionCallbacks

  # called when the callback object is linked with a new client
  def on_open client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when data is available
  def on_message client, data
     client.is_a?(Iodine::Connection) # => true
  end

  # called when the server is shutting down, before closing the client
  # (it's still possible to send messages to the client)
  def on_shutdown client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when the client is closed (no longer available)
  def on_close client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when all the previous calls to `client.write` have completed
  # (the local buffer was drained and is now empty)
  def on_drained client
     client.is_a?(Iodine::Connection) # => true
  end

  # called when timeout was reached, allowing a `ping` to be sent
  def ping client
     client.is_a?(Iodine::Connection) # => true
     clint.close() # close connection on timeout is the default
  end

  # Allows the module to be used as a static callback object (avoiding object allocation)
  extend self
end
All connection related actions can be performed using the methods provided through this class.


#close ⇒ Object
Schedules the connection to be closed.

#publish(*args) ⇒ Object
Publishes a message to a channel.

#subscribe(*args) ⇒ Object
Subscribes to a Pub/Sub stream / channel or replaces an existing subscription.

#unsubscribe(name) ⇒ Object
Unsubscribes from a Pub/Sub stream / channel.

#write(data) ⇒ Object
Writes data to the connection asynchronously.

#protocol ⇒ Object
Returns the connection's protocol Symbol (:sse, :websocket or :raw).

*/
static void Init_iodine_connection(void) { // clang-format on
  VALUE m = rb_define_class_under(iodine_rb_IODINE, "Connection", rb_cObject);
  rb_define_alloc_func(m, iodine_connection_alloc);
  rb_define_method(m, "initialize", iodine_connection_initialize, -1);

  rb_define_method(m, "env", iodine_connection_env, 0);
  rb_define_method(m, "handler", iodine_connection_handler, 0);
  rb_define_method(m, "handler=", iodine_connection_handler_set, 1);
  rb_define_method(m, "open?", iodine_connection_is_open, 0);
  rb_define_method(m, "pubsub?", iodine_connection_has_pubsub, 0);
  rb_define_method(m, "pending", iodine_connection_pending, 0);
  rb_define_method(m, "close", iodine_connection_close, 0);
  rb_define_method(m, "write", iodine_connection_write, 1);

  rb_define_method(m, "subscribe", iodine_connection_subscribe, -1);
  rb_define_singleton_method(m,
                             "subscribe",
                             iodine_connection_subscribe_klass,
                             -1);
  rb_define_singleton_method(iodine_rb_IODINE,
                             "subscribe",
                             iodine_connection_subscribe_klass,
                             -1);
}
