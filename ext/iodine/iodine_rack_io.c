/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_rack_io.h"

#include "iodine.h"

#include <ruby/encoding.h>
#include <ruby/io.h>
#include <unistd.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* IodineRackIO manages a minimal interface to act as an IO wrapper according to
these Rack specifications:

The input stream is an IO-like object which contains the raw HTTP POST data.
When applicable, its external encoding must be “ASCII-8BIT” and it must be
opened in binary mode, for Ruby 1.9 compatibility. The input stream must respond
to gets, each, read and rewind.

gets must be called without arguments and return a string, or nil on EOF.

read behaves like IO#read. Its signature is read([length, [buffer]]). If given,
length must be a non-negative Integer (>= 0) or nil, and buffer must be a String
and may not be nil. If length is given and not nil, then this method reads at
most length bytes from the input stream. If length is not given or nil, then
this method reads all data until  EOF. When EOF is reached, this method returns
nil if length is given and not nil, or “” if length is not given or is nil. If
buffer is given, then the read data will be placed into buffer instead of a
newly created String object.

each must be called without arguments and only yield Strings.

rewind must be called without arguments. It rewinds the input stream back to the
beginning. It must not raise Errno::ESPIPE: that is, it may not be a pipe or a
socket. Therefore, handler developers must buffer the input data into some
rewindable object if the underlying input stream is not rewindable.

close must never be called on the input stream.

*/

/* *****************************************************************************
Core data / helpers
*/

static VALUE rRackIO;

static ID env_id;
static ID io_id;

static VALUE R_INPUT; /* rack.input */
static VALUE hijack_func_sym;
static VALUE TCPSOCKET_CLASS;
static ID for_fd_id;
static ID iodine_fd_var_id;
static ID iodine_new_func_id;
static rb_encoding *IodineUTF8Encoding;
static rb_encoding *IodineBinaryEncoding;

#define set_handle(object, handle)                                             \
  rb_ivar_set((object), iodine_fd_var_id, ULL2NUM((uintptr_t)handle))

inline static http_s *get_handle(VALUE obj) {
  VALUE i = rb_ivar_get(obj, iodine_fd_var_id);
  return (http_s *)FIX2ULONG(i);
}

/* *****************************************************************************
IO API
*/

static inline FIOBJ get_data(VALUE self) {
  VALUE i = rb_ivar_get(self, io_id);
  return (FIOBJ)FIX2ULONG(i);
}

static VALUE rio_rewind(VALUE self) {
  FIOBJ io = get_data(self);
  if (!FIOBJ_TYPE_IS(io, FIOBJ_T_DATA))
    return Qnil;
  fiobj_data_seek(io, 0);
  return INT2NUM(0);
}
/**
Gets returns a line. this is okay for small lines,
but shouldn't really be used.

Limited to ~ 1Mb of a line length.
*/
static VALUE rio_gets(VALUE self) {
  FIOBJ io = get_data(self);
  if (!FIOBJ_TYPE_IS(io, FIOBJ_T_DATA))
    return Qnil;
  fio_str_info_s line = fiobj_data_gets(io);
  if (line.len) {
    VALUE buffer = rb_str_new(line.data, line.len);
    // make sure the buffer is binary encoded.
    rb_enc_associate(buffer, IodineBinaryEncoding);
    return buffer;
  }
  return Qnil;
}

// Reads data from the IO, according to the Rack specifications for `#read`.
static VALUE rio_read(int argc, VALUE *argv, VALUE self) {
  FIOBJ io = get_data(self);
  VALUE buffer = Qnil;
  uint8_t ret_nil = 0;
  ssize_t len = 0;
  if (!FIOBJ_TYPE_IS(io, FIOBJ_T_DATA)) {
    return (argc > 0 && argv[0] != Qnil) ? Qnil : rb_str_buf_new(0);
  }

  // get the buffer object if given
  if (argc == 2) {
    Check_Type(argv[1], T_STRING);
    buffer = argv[1];
  }
  // get the length object, if given
  if (argc > 0 && argv[0] != Qnil) {
    Check_Type(argv[0], T_FIXNUM);
    len = FIX2LONG(argv[0]);
    if (len < 0)
      rb_raise(rb_eRangeError, "length should be bigger then 0.");
    if (len == 0)
      return rb_str_buf_new(0);
    ret_nil = 1;
  }
  // return if we're at the EOF.
  fio_str_info_s buf = fiobj_data_read(io, len);
  if (buf.len) {
    // create the buffer if we don't have one.
    if (buffer == Qnil) {
      // make sure the buffer is binary encoded.
      buffer = rb_enc_str_new(buf.data, buf.len, IodineBinaryEncoding);
    } else {
      // make sure the buffer is binary encoded.
      rb_enc_associate(buffer, IodineBinaryEncoding);
      if (rb_str_capacity(buffer) < (size_t)buf.len)
        rb_str_resize(buffer, buf.len);
      memcpy(RSTRING_PTR(buffer), buf.data, buf.len);
      rb_str_set_len(buffer, buf.len);
    }
    return buffer;
  }
  return ret_nil ? Qnil : rb_str_buf_new(0);
}

// Does nothing - this is controlled by the server.
static VALUE rio_close(VALUE self) {
  // FIOBJ io = get_data(self);
  // fiobj_free(io); // we don't call fiobj_dup, do we?
  rb_ivar_set(self, io_id, INT2NUM(0));
  (void)self;
  return Qnil;
}

// Passes each line of the input to the block. This should be avoided.
static VALUE rio_each(VALUE self) {
  rb_need_block();
  rio_rewind(self);
  VALUE str = Qnil;
  while ((str = rio_gets(self)) != Qnil) {
    rb_yield(str);
  }
  return self;
}

/* *****************************************************************************
Hijacking
*/

// defined by iodine_http
extern VALUE IODINE_R_HIJACK;    // for Rack: rack.hijack
extern VALUE IODINE_R_HIJACK_CB; // for Rack: rack.hijack
extern VALUE IODINE_R_HIJACK_IO; // for Rack: rack.hijack_io

static VALUE rio_get_io(int argc, VALUE *argv, VALUE self) {
  if (TCPSOCKET_CLASS == Qnil)
    return Qfalse;
  VALUE env = rb_ivar_get(self, env_id);
  http_s *h = get_handle(self);
  if (h == NULL) {
    /* we're repeating ourselves, aren't we? */
    VALUE io = rb_hash_aref(env, IODINE_R_HIJACK_IO);
    return io;
  }
  // mark update
  set_handle(self, NULL);
  // hijack the IO object
  intptr_t uuid = http_hijack(h, NULL);
  VALUE fd = INT2FIX(fio_uuid2fd(uuid));
  // VALUE new_io = how the fuck do we create a new IO from the fd?
  VALUE new_io = IodineCaller.call2(TCPSOCKET_CLASS, for_fd_id, 1,
                                    &fd); // TCPSocket.for_fd(fd) ... cool...
  rb_hash_aset(env, IODINE_R_HIJACK_IO, new_io);
  if (argc)
    rb_hash_aset(env, IODINE_R_HIJACK_CB, *argv);
  return new_io;
}

/* *****************************************************************************
C land API
*/

// new object
static VALUE new_rack_io(http_s *h, VALUE env) {
  VALUE rack_io = rb_funcall2(rRackIO, iodine_new_func_id, 0, NULL);
  rb_ivar_set(rack_io, io_id, ULL2NUM(h->body));
  set_handle(rack_io, h);
  rb_ivar_set(rack_io, env_id, env);
  rb_hash_aset(env, IODINE_R_INPUT, rack_io);
  rb_hash_aset(env, IODINE_R_HIJACK, rb_obj_method(rack_io, hijack_func_sym));
  return rack_io;
}

static void close_rack_io(VALUE rack_io) {
  // rio_close(rack_io);
  rb_ivar_set(rack_io, io_id, INT2NUM(0));
  set_handle(rack_io, NULL); /* this disables hijacking. */
}

// initialize library
static void init_rack_io(void) {
  IodineUTF8Encoding = rb_enc_find("UTF-8");
  IodineBinaryEncoding = rb_enc_find("binary");
  rRackIO = rb_define_class_under(IodineBaseModule, "RackIO", rb_cObject);

  io_id = rb_intern("rack_io");
  env_id = rb_intern("env");
  for_fd_id = rb_intern("for_fd");
  iodine_fd_var_id = rb_intern("fd");
  iodine_new_func_id = rb_intern("new");
  hijack_func_sym = ID2SYM(rb_intern("_hijack"));

  TCPSOCKET_CLASS = rb_const_get(rb_cObject, rb_intern("TCPSocket"));
  // IO methods

  rb_define_method(rRackIO, "rewind", rio_rewind, 0);
  rb_define_method(rRackIO, "gets", rio_gets, 0);
  rb_define_method(rRackIO, "read", rio_read, -1);
  rb_define_method(rRackIO, "close", rio_close, 0);
  rb_define_method(rRackIO, "each", rio_each, 0);
  rb_define_method(rRackIO, "_hijack", rio_get_io, -1);
}

////////////////////////////////////////////////////////////////////////////
// the API interface
struct IodineRackIO IodineRackIO = {
    .create = new_rack_io,
    .close = close_rack_io,
    .init = init_rack_io,
};
