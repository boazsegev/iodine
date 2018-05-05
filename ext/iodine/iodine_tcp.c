#include "iodine.h"
#include <ruby/encoding.h>
#include <ruby/io.h>

#include "facil.h"

/* *****************************************************************************
Static stuff
***************************************************************************** */
static ID call_id;
static rb_encoding *IodineBinaryEncoding;

static VALUE port_id;
static VALUE address_id;
static VALUE handler_id;

/* *****************************************************************************
Raw TCP/IP Protocol
***************************************************************************** */

#define IODINE_MAX_READ 8192

typedef struct {
  protocol_s p;
  VALUE io;
} iodine_protocol_s;

typedef struct {
  VALUE io;
  ssize_t len;
  char buffer[IODINE_MAX_READ];
} iodine_buffer_s;

/**
 * A string to identify the protocol's service.
 */
static const char *iodine_tcp_service = "iodine TCP/IP raw connection";

/**
 * Converts an iodine_buffer_s pointer to a Ruby string.
 */
static void *iodine_tcp_on_data_in_GIL(void *b_) {
  iodine_buffer_s *b = b_;
  if (!b) {
    fprintf(stderr, "FATAL ERROR: (iodine->tcp/ip->on_data->GIL) WTF?!\n");
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
static void iodine_tcp_on_data(intptr_t uuid, protocol_s *protocol) {
  iodine_buffer_s buffer;
  buffer.len = sock_read(uuid, buffer.buffer, IODINE_MAX_READ);
  if (buffer.len <= 0) {
    return;
  }
  buffer.io = ((iodine_protocol_s *)protocol)->io;
  IodineCaller.enterGVL(iodine_tcp_on_data_in_GIL, &buffer);
  if (buffer.len == IODINE_MAX_READ) {
    facil_force_event(uuid, FIO_EVENT_ON_DATA);
  }
}

/** called when the socket is ready to be written to. */
static void iodine_tcp_on_ready(intptr_t uuid, protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_DRAINED, Qnil);
  (void)uuid;
}

/**
 * Called when the server is shutting down, immediately before closing the
 * connection.
 */
static void iodine_tcp_on_shutdown(intptr_t uuid, protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_SHUTDOWN, Qnil);
  (void)uuid;
}

/** Called when the connection was closed, but will not run concurrently */

static void iodine_tcp_on_close(intptr_t uuid, protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_CLOSE, Qnil);
  free(p);
  (void)uuid;
}

/** called when a connection's timeout was reached */
static void iodine_tcp_ping(intptr_t uuid, protocol_s *protocol) {
  iodine_protocol_s *p = (iodine_protocol_s *)protocol;
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_PING, Qnil);
  (void)uuid;
}

/** called when a connection opens */
static void iodine_tcp_on_open(intptr_t uuid, void *udata) {
  iodine_protocol_s *p = malloc(sizeof(*p));
  if (!p) {
    perror("FATAL ERROR: No Memory!");
    exit(errno);
  }
  VALUE handler = IodineCaller.call((VALUE)udata, call_id);
  if (handler == Qnil || handler == Qfalse || handler == Qtrue) {
    sock_close(uuid);
    free(p);
    return;
  }
  /* temporary, in case `iodine_connection_new` invokes the GC */
  IodineStore.add(handler);
  *p = (iodine_protocol_s){
      .p =
          {
              .service = iodine_tcp_service,
              .on_data = iodine_tcp_on_data,
              .on_ready = iodine_tcp_on_ready,
              .on_shutdown = iodine_tcp_on_shutdown,
              .on_close = iodine_tcp_on_close,
              .ping = iodine_tcp_ping,
          },
      .io = iodine_connection_new(.type = IODINE_CONNECTION_RAW, .uuid = uuid,
                                  .arg = p, .handler = handler),
  };
  /* clear away (remember the connection object manages these concerns) */
  IodineStore.remove(handler);
  facil_attach(uuid, &p->p);
  iodine_connection_fire_event(p->io, IODINE_CONNECTION_ON_OPEN, Qnil);
}

/** called when the listening socket is destroyed */
static void iodine_tcp_on_finish(intptr_t uuid, void *udata) {
  IodineStore.remove((VALUE)udata);
  (void)uuid;
}

/* *****************************************************************************
The Ruby API implementation
***************************************************************************** */

// clang-format off
/**
The {listen} method accepts a single Hash argument with the following optional
keys:

:port :: The port to listen to, deafults to 0 (using a Unix socket)
:address :: The address to listen to, which could be a Unix Socket path as well as an IPv4 / IPv6 address. Deafults to 0.0.0.0 (or the IPv6 equivelant).
:handler :: An object that answers the `call` method (i.e., a Proc).

The method also accepts an optional block.

Either a block or the :handler key MUST be present.

The handler object should return a connection handler that supports the following callbacks (see also {Iodine::Connection}):

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
        # returns a connection handler, the ChatHandler module (self) in our example.
        def self.call
          self
        end
      end
      # we can bothe the `handler` keuword or a block, anything that answers #call.
      Iodine.listen(port: "3000", handler: ChatHandler)
      # start the service
      Iodine.threads = 1
      Iodine.start



Returns the handler object used.
*/
static VALUE iodine_tcp_listen(VALUE self, VALUE args) {
  // clang-format on
  Check_Type(args, T_HASH);
  VALUE rb_port = rb_hash_aref(args, port_id);
  VALUE rb_address = rb_hash_aref(args, address_id);
  VALUE rb_handler = rb_hash_aref(args, handler_id);
  if (rb_handler == Qnil) {
    rb_need_block();
    rb_handler = rb_block_proc();
  }
  IodineStore.add(rb_handler);
  if (rb_address != Qnil) {
    Check_Type(rb_address, T_STRING);
  }
  if (rb_port != Qnil) {
    Check_Type(rb_port, T_STRING);
  }
  if (facil_listen(.port = (rb_port == Qnil ? NULL : StringValueCStr(rb_port)),
                   .address =
                       (rb_address == Qnil ? NULL
                                           : StringValueCStr(rb_address)),
                   .on_open = iodine_tcp_on_open,
                   .on_finish = iodine_tcp_on_finish,
                   .udata = (void *)rb_handler) == -1) {
    IodineStore.remove(rb_handler);
    rb_raise(rb_eRuntimeError,
             "failed to listen to requested address, unknown error.");
  }
  return rb_handler;
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
  IodineBinaryEncoding = rb_enc_find("binary");

  rb_define_module_function(IodineModule, "listen", iodine_tcp_listen, 1);
}
