#include "iodine_connection.h"

#include "facil.h"
#include "fio_mem.h"
#include "websockets.h"

#include <ruby/io.h>

/* *****************************************************************************
Constants in use
***************************************************************************** */

static ID new_id;
static ID call_id;
static ID on_open_id;
static ID on_message_id;
static ID on_drained_id;
static ID on_shutdown_id;
static ID on_closed_id;
static VALUE ConnectionKlass;
static rb_encoding *IodineUTF8Encoding;
static VALUE WebSocketSymbol;
static VALUE SSESymbol;
static VALUE RAWSymbol;

/* *****************************************************************************
C <=> Ruby Data allocation
***************************************************************************** */

typedef struct {
  iodine_connection_s info;
  uint8_t answers_on_message;
  uint8_t answers_on_drained;
  /* these are one-shot, but the CPU cache might have the data, so set it */
  uint8_t answers_on_open;
  uint8_t answers_on_shutdown;
  uint8_t answers_on_closed;
} iodine_connection_data_s;

/* a callback for the GC (marking active objects) */
static void iodine_connection_data_mark(void *c_) {
  iodine_connection_data_s *c = c_;
  rb_gc_mark(c->info.handler);
}
/* a callback for the GC (marking active objects) */
static void iodine_connection_data_free(void *c_) { free(c_); }

size_t iodine_connection_data_size(const void *c_) {
  return sizeof(iodine_connection_data_s);
  (void)c_;
}

static const rb_data_type_t iodine_connection_data_type = {
    .wrap_struct_name = "IodineConnectionData",
    .function =
        {
            .dmark = iodine_connection_data_mark,
            .dfree = iodine_connection_data_free,
            .dsize = iodine_connection_data_size,
        },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/* Iodine::PubSub::Engine.allocate */
static VALUE iodine_connection_data_alloc_c(VALUE self) {
  iodine_connection_data_s *c = malloc(sizeof(*c));
  if (TYPE(self) == T_CLASS)
    *c = (iodine_connection_data_s){
        .info.handler = (VALUE)0, .info.uuid = -1,
    };
  return TypedData_Wrap_Struct(self, &iodine_connection_data_type, c);
}

static inline iodine_connection_data_s *iodine_connection_ruby2C(VALUE self) {
  iodine_connection_data_s *c = NULL;
  TypedData_Get_Struct(self, iodine_connection_data_s,
                       &iodine_connection_data_type, c);
  return c;
}

static inline iodine_connection_data_s *
iodine_connection_validate_data(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_ruby2C(self);
  if (c == NULL || c->info.handler == Qnil || c->info.uuid == -1 ||
      sock_isclosed(c->info.uuid)) {
    return NULL;
  }
  return c;
}

/* *****************************************************************************
Ruby Connection Methods - write, close open? pending
***************************************************************************** */

/**
 * Writes data to the connection asynchronously.
 *
 * In effect, the `write` call does nothing, it only schedules the data to be
 * sent and marks the data as pending.
 *
 * Use {pending} to test how many `write` operations are pending completion
 * (`on_drained(client)` will be called when they complete).
 */
VALUE iodine_connection_write(VALUE self, VALUE data) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (!c) {
    rb_raise(rb_eIOError, "Connection closed or invalid.");
  }
  if (c->info.type == IODINE_CONNECTION_WEBSOCKET) {
    websocket_close(c->info.arg);
  }
  switch (c->info.type) {
  case IODINE_CONNECTION_WEBSOCKET:
    /* WebSockets*/
    websocket_write(c->info.arg, RSTRING_PTR(data), RSTRING_LEN(data),
                    rb_enc_get(data) == IodineUTF8Encoding);
    return Qtrue;
    break;
  case IODINE_CONNECTION_SSE:
    /* SSE */
    if (rb_enc_get(data) == IodineUTF8Encoding) {
      http_sse_write(c->info.arg, .data = {.data = RSTRING_PTR(data),
                                           .len = RSTRING_LEN(data)});
      return Qtrue;
    }
    rb_raise(rb_eEncodingError,
             "This Connection type (SSE) requires data to be UTF-8 encoded.");
    break;
  case IODINE_CONNECTION_RAW: /* fallthrough */
  default: {
    size_t len = RSTRING_LEN(data);
    char *copy = fio_malloc(len);
    if (!copy) {
      rb_raise(rb_eNoMemError, "failed to allocate memory for network buffer!");
    }
    memcpy(copy, RSTRING_PTR(data), len);
    sock_write2(.uuid = c->info.uuid, .buffer = copy, .length = len,
                .dealloc = fio_free);
    return Qtrue;
  } break;
  }
  return Qnil;
}

/**
 * Schedules the connection to be closed.
 *
 * The connection will be closed once all the scheduled `write` operations have
 * been completed (or failed).
 */
VALUE iodine_connection_close(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c) {
    if (c->info.type == IODINE_CONNECTION_WEBSOCKET) {
      websocket_close(c->info.arg);
    } else {
      sock_close(c->info.uuid);
    }
  }

  return Qnil;
}
/** Returns true if the connection appears to be open (no known issues). */
VALUE iodine_connection_is_open(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c) {
    return Qtrue;
  }
  return Qfalse;
}
/**
 * Returns the number of pending `write` operations that need to complete
 * before the next `on_drained` callback is called.
 */
VALUE iodine_connection_pending(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (!c) {
    return INT2NUM(-1);
  }
  return SIZET2NUM((sock_pending(c->info.uuid)));
}

/** Returns the connection's type (`:sse`, `:websocket`, etc'). */
VALUE iodine_connection_type(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  switch (c->info.type) {
  case IODINE_CONNECTION_WEBSOCKET:
    return WebSocketSymbol;
    break;
  case IODINE_CONNECTION_SSE:
    return SSESymbol;
    break;
  case IODINE_CONNECTION_RAW: /* fallthrough */
    return RAWSymbol;
    break;
  }
  return Qnil;
}

/**
 * Returns the timeout / `ping` interval for the connection.
 *
 * Returns nil on error.
 */
VALUE iodine_connection_timeout_get(VALUE self) {
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c) {
    size_t tout = (size_t)facil_get_timeout(c->info.uuid);
    return SIZET2NUM(tout);
  }
  return Qnil;
}

/**
 * Sets the timeout / `ping` interval for the connection (up to 255 seconds).
 *
 * Returns nil on error.
 */
VALUE iodine_connection_timeout_set(VALUE self, VALUE timeout) {
  Check_Type(timeout, T_FIXNUM);
  int tout = NUM2INT(timeout);
  if (tout < 0 || tout > 255) {
    rb_raise(rb_eRangeError, "timeout out of range.");
    return Qnil;
  }
  iodine_connection_data_s *c = iodine_connection_validate_data(self);
  if (c) {
    facil_set_timeout(c->info.uuid, (uint8_t)tout);
    return timeout;
  }
  return Qnil;
}

/* *****************************************************************************
Published C functions
***************************************************************************** */

#undef iodine_connection_new
VALUE iodine_connection_new(iodine_connection_s args) {
  VALUE connection = IodineCaller.call(ConnectionKlass, new_id);
  if (connection == Qnil) {
    return Qnil;
  }
  iodine_connection_data_s *data = iodine_connection_ruby2C(connection);
  if (data == NULL) {
    return Qnil;
  }
  data->info = args;
  // rb_obj_method(VALUE, VALUE)
  data->answers_on_open = (rb_respond_to(args.handler, on_open_id) != 0);
  data->answers_on_message = (rb_respond_to(args.handler, on_message_id) != 0);
  data->answers_on_drained = (rb_respond_to(args.handler, on_drained_id) != 0);
  data->answers_on_shutdown =
      (rb_respond_to(args.handler, on_shutdown_id) != 0);
  data->answers_on_closed = (rb_respond_to(args.handler, on_closed_id) != 0);
  return connection;
}

/** Fires a connection object's event */
void iodine_connection_fire_event(VALUE connection,
                                  iodine_connection_event_type_e ev,
                                  VALUE msg) {
  if (connection == Qnil) {
    return;
  }
  iodine_connection_data_s *data = iodine_connection_validate_data(connection);
  if (!data) {
    return;
  }
  VALUE args[2] = {connection, msg};
  switch (ev) {
  case IODINE_CONNECTION_ON_OPEN:
    IodineCaller.call2(data->info.handler, on_open_id, 1, args);
    break;
  case IODINE_CONNECTION_ON_MESSAGE:
    IodineCaller.call2(data->info.handler, on_message_id, 2, args);
    break;
  case IODINE_CONNECTION_ON_DRAINED:
    IodineCaller.call2(data->info.handler, on_drained_id, 1, args);
    break;
  case IODINE_CONNECTION_ON_SHUTDOWN:
    IodineCaller.call2(data->info.handler, on_shutdown_id, 1, args);
    break;
  case IODINE_CONNECTION_ON_CLOSE:
    IodineCaller.call2(data->info.handler, on_closed_id, 1, args);
    data->info.handler = Qnil;
    data->info.uuid = -1;
    data->info.arg = NULL;
    break;
  default:
    break;
  }
}

void iodine_connection_init(void) {
  // set used constants
  call_id = rb_intern2("new", 3);
  call_id = rb_intern2("call", 4);
  on_open_id = rb_intern("on_open");
  on_message_id = rb_intern("on_message");
  on_drained_id = rb_intern("on_drained");
  on_shutdown_id = rb_intern("on_shutdown");
  on_closed_id = rb_intern("on_closed");
  IodineUTF8Encoding = rb_enc_find("UTF-8");
  // should these be globalized?
  WebSocketSymbol = ID2SYM(rb_intern("websocket"));
  SSESymbol = ID2SYM(rb_intern("sse"));
  RAWSymbol = ID2SYM(rb_intern("raw"));

  // define the Connection Class and it's methods
  ConnectionKlass =
      rb_define_class_under(IodineModule, "Connection", rb_cObject);
  rb_define_alloc_func(ConnectionKlass, iodine_connection_data_alloc_c);
  rb_define_method(ConnectionKlass, "write", iodine_connection_write, 1);
  rb_define_method(ConnectionKlass, "close", iodine_connection_close, 0);
  rb_define_method(ConnectionKlass, "open?", iodine_connection_is_open, 0);
  rb_define_method(ConnectionKlass, "pending", iodine_connection_pending, 0);
  rb_define_method(ConnectionKlass, "type", iodine_connection_type, 0);
  rb_define_method(ConnectionKlass, "timeout", iodine_connection_timeout_get,
                   0);
  rb_define_method(ConnectionKlass, "timeout=", iodine_connection_timeout_set,
                   1);
}
