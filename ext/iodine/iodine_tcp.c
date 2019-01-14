#include "iodine.h"
#include <ruby/encoding.h>
#include <ruby/io.h>

#define FIO_INCLUDE_STR
#include "fio.h"

/* *****************************************************************************
Static stuff
***************************************************************************** */
static ID call_id;
static ID on_closed_id;
static rb_encoding *IodineBinaryEncoding;

static VALUE port_id;
static VALUE address_id;
static VALUE handler_id;
static VALUE timeout_id;

/* *****************************************************************************
Raw TCP/IP Protocol
***************************************************************************** */

#define IODINE_MAX_READ 8192

typedef struct {
  fio_protocol_s p;
  VALUE io;
} iodine_protocol_s;

typedef struct {
  VALUE io;
  ssize_t len;
  char buffer[IODINE_MAX_READ];
} iodine_buffer_s;

/**
 * Converts an iodine_buffer_s pointer to a Ruby string.
 */
static void *iodine_tcp_on_data_in_GIL(void *b_) {
  iodine_buffer_s *b = b_;
  if (!b) {
    FIO_LOG_FATAL("(iodine->tcp/ip->on_data->GIL) WTF?!\n");
    exit(-1);
  }
  VALUE data = IodineStore.add(rb_str_new(b->buffer, b->len));
  rb_enc_associate(data, IodineBinaryEncoding);
  iodine_connection_fire_event(b->io, IODINE_CONNECTION_ON_MESSAGE, data);
  IodineStore.remove(data);
  return NULL;
  // return (void *)IodineStore.add(rb_usascii_str_new((const char *)b->buffer,
  // b->len));
}

/** Called when a data is available, but will not run concurrently */
static void iodine_tcp_on_data(intptr_t uuid, fio_protocol_s *protocol) {
  iodine_buffer_s buffer;
  buffer.len = fio_read(uuid, buffer.buffer, IODINE_MAX_READ);
  if (buffer.len <= 0) {
    return;
  }
  buffer.io = ((iodine_protocol_s *)protocol)->io;
  IodineCaller.enterGVL(iodine_tcp_on_data_in_GIL, &buffer);
  if (buffer.len == IODINE_MAX_READ) {
    fio_force_event(uuid, FIO_EVENT_ON_DATA);
  }
}

/** called when the socket is ready to be written to. */
static void iodine_tcp_on_ready(intptr_t uuid, fio_protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_DRAINED, Qnil);
  (void)uuid;
}

/**
 * Called when the server is shutting down, immediately before closing the
 * connection.
 */
static uint8_t iodine_tcp_on_shutdown(intptr_t uuid, fio_protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_SHUTDOWN, Qnil);
  return 0;
  (void)uuid;
}

/** Called when the connection was closed, but will not run concurrently */

static void iodine_tcp_on_close(intptr_t uuid, fio_protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_CLOSE, Qnil);
  free(p);
  (void)uuid;
}

/** called when a connection's timeout was reached */
static void iodine_tcp_ping(intptr_t uuid, fio_protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_PING, Qnil);
  (void)uuid;
}

/** fio_listen callback, called when a connection opens */
static void iodine_tcp_on_open(intptr_t uuid, void *udata) {
  if (!fio_is_valid(uuid))
    return;
  VALUE handler = IodineCaller.call((VALUE)udata, call_id);
  IodineStore.add(handler);
  iodine_tcp_attch_uuid(uuid, handler);
  IodineStore.remove(handler);
}

/** called when the listening socket is destroyed */
static void iodine_tcp_on_finish(intptr_t uuid, void *udata) {
  IodineStore.remove((VALUE)udata);
  (void)uuid;
}

/**
 * The `on_connect` callback should either call `fio_attach` or close the
 * connection.
 */
static void iodine_tcp_on_connect(intptr_t uuid, void *udata) {
  iodine_tcp_attch_uuid(uuid, (VALUE)udata);
  IodineStore.remove((VALUE)udata);
}

/**
 * The `on_fail` is called when a socket fails to connect. The old sock UUID
 * is passed along.
 */
static void iodine_tcp_on_fail(intptr_t uuid, void *udata) {
  IodineStore.remove((VALUE)udata);
}

/* *****************************************************************************
The Ruby API implementation
***************************************************************************** */

// clang-format off
/**
The {listen} method instructs iodine to listen to incoming connections using either TCP/IP or Unix sockets.

The method accepts a single Hash argument with the following optional keys:

:port :: The port to listen to, deafults to nil (using a Unix socket)
:address :: The address to listen to, which could be a Unix Socket path as well as an IPv4 / IPv6 address. Deafults to 0.0.0.0 (or the IPv6 equivelant).
:handler :: An object that answers the `call` method (i.e., a Proc).

The method also accepts an optional block.

Either a block or the :handler key MUST be present.

The handler Proc (or object) should return a connection callback object that supports the following callbacks (see also {Iodine::Connection}):

on_open(client) :: called after a connection was established
on_message(client, data) :: called when incoming data is available. Data may be fragmented.
on_drained(client) :: called when all the pending `client.write` events have been processed (see {Iodine::Connection#pending}).
ping(client) :: called whenever a timeout has occured (see {Iodine::Connection#timeout=}).
on_shutdown(client) :: called if the server is shutting down. This is called before the connection is closed.
on_close(client) :: called when the connection with the client was closed.

The `client` argument is an {Iodine::Connection} instance that represents the connection / the client.

Here's a telnet based chat-room example:

      require 'iodine'
      # define the protocol for our service
      module ChatHandler
        def self.on_open(client)
          # Set a connection timeout
          client.timeout = 10
          # subscribe to the chat channel.
          client.subscribe :chat
          # Write a welcome message
          client.publish :chat, "new member entered the chat\r\n"
        end
        # this is called for incoming data - note data might be fragmented.
        def self.on_message(client, data)
          # publish the data we received
          client.publish :chat, data
          # close the connection when the time comes
          client.close if data =~ /^bye[\n\r]/
        end
        # called whenever timeout occurs.
        def self.ping(client)
          client.write "System: quite, isn't it...?\r\n"
        end
        # called if the connection is still open and the server is shutting down.
        def self.on_shutdown(client)
          # write the data we received
          client.write "Chat server going away. Try again later.\r\n"
        end
        # returns the callback object (self).
        def self.call
          self
        end
      end
      # we use can both the `handler` keyword or a block, anything that answers #call.
      Iodine.listen(port: "3000", handler: ChatHandler)
      # start the service
      Iodine.threads = 1
      Iodine.start



Returns the handler object used.
*/
intptr_t iodine_tcp_listen(iodine_connection_args_s args) {
  // clang-format on
  IodineStore.add(args.handler);
  return fio_listen(.port = args.port.data, .address = args.address.data,
                    .on_open = iodine_tcp_on_open,
                    .on_finish = iodine_tcp_on_finish, .tls = args.tls,
                    .udata = (void *)args.handler);
}

// clang-format off
/**
The {connect} method instructs iodine to connect to a server using either TCP/IP or Unix sockets.

The method accepts a single Hash argument with the following optional keys:

:port :: The port to listen to, deafults to 0 (using a Unix socket)
:address :: The address to listen to, which could be a Unix Socket path as well as an IPv4 / IPv6 address. Deafults to 0.0.0.0 (or the IPv6 equivelant).
:handler :: A connection callback object that supports the following same callbacks listen in the {listen} method's documentation.
:timeout :: An integer timeout for connection establishment (doen't effect the new connection's timeout. Should be in the rand of 0..255.
:tls :: An {Iodine::TLS} object (optional) for secure connections.

The method also accepts an optional block.

Either a block or the :handler key MUST be present.

If the connection fails, only the `on_close` callback will be called (with a `nil` client).

Returns the handler object used.
*/
intptr_t iodine_tcp_connect(iodine_connection_args_s args){
  // clang-format on
  IodineStore.add(args.handler);
  return fio_connect(.port = args.port.data, .address = args.address.data,
                     .on_connect = iodine_tcp_on_connect, .tls = args.tls,
                     .on_fail = iodine_tcp_on_fail, .timeout = args.ping,
                     .udata = (void *)args.handler);
}

// clang-format off
/**
The {attach_fd} method instructs iodine to attach a socket to the server using it's numerical file descriptor.

This is faster than attaching a Ruby IO object since it allows iodine to directly call the system's read/write methods. However, this doesn't support TLS/SSL connections.

This method requires two objects, a file descriptor (`fd`) and a callback object.

See {listen} for details about the callback object.

Returns the callback object (handler) used.
*/
static VALUE iodine_tcp_attach_fd(VALUE self, VALUE fd, VALUE handler) {
  // clang-format on
  Check_Type(fd, T_FIXNUM);
  if (handler == Qnil || handler == Qfalse || handler == Qtrue) {
    rb_raise(rb_eArgError, "A callback object must be provided.");
  }
  IodineStore.add(handler);
  int other = dup(NUM2INT(fd));
  if (other == -1) {
    rb_raise(rb_eIOError, "invalid fd.");
  }
  intptr_t uuid = fio_fd2uuid(other);
  iodine_tcp_attch_uuid(uuid, handler);
  IodineStore.remove(handler);
  return handler;
  (void)self;
}

/* *****************************************************************************
Add the Ruby API methods to the Iodine object
***************************************************************************** */

void iodine_init_tcp_connections(void) {
  call_id = rb_intern2("call", 4);
  port_id = IodineStore.add(rb_id2sym(rb_intern("port")));
  address_id = IodineStore.add(rb_id2sym(rb_intern("address")));
  handler_id = IodineStore.add(rb_id2sym(rb_intern("handler")));
  timeout_id = IodineStore.add(rb_id2sym(rb_intern("timeout")));
  on_closed_id = rb_intern("on_closed");

  IodineBinaryEncoding = rb_enc_find("binary");

  rb_define_module_function(IodineModule, "attach_fd", iodine_tcp_attach_fd, 2);
}

/* *****************************************************************************
Allow uuid attachment
***************************************************************************** */

/** assigns a protocol and IO object to a handler */
void iodine_tcp_attch_uuid(intptr_t uuid, VALUE handler) {
  FIO_LOG_DEBUG("Iodine attaching handler %p to uuid %p", (void *)handler,
                (void *)uuid);
  if (handler == Qnil || handler == Qfalse || handler == Qtrue) {
    fio_close(uuid);
    return;
  }
  /* temporary, in case `iodine_connection_new` invokes the GC */
  iodine_protocol_s *p = malloc(sizeof(*p));
  FIO_ASSERT_ALLOC(p);
  *p = (iodine_protocol_s){
      .p =
          {
              .on_data = iodine_tcp_on_data,
              .on_ready = NULL /* set only after the on_open callback */,
              .on_shutdown = iodine_tcp_on_shutdown,
              .on_close = iodine_tcp_on_close,
              .ping = iodine_tcp_ping,
          },
      .io = iodine_connection_new(.type = IODINE_CONNECTION_RAW, .uuid = uuid,
                                  .arg = p, .handler = handler),
  };
  /* clear away (remember the connection object manages these concerns) */
  fio_attach(uuid, &p->p);
  if (fio_is_valid(uuid)) {
    iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_OPEN, Qnil);
    p->p.on_ready = iodine_tcp_on_ready;
    fio_force_event(uuid, FIO_EVENT_ON_READY);
  } else {
    FIO_LOG_DEBUG(
        "Iodine couldn't attach handler %p to uuid %p - invalid uuid.",
        (void *)handler, (void *)uuid);
  }
}
