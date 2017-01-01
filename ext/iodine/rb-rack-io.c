/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
// clang-format off
#include "rb-rack-io.h"
#include "iodine_core.h"
#include <ruby/io.h>
#include <ruby/encoding.h>
#include <unistd.h>
// clang-format on

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rb-call.h"

/* RackIO manages a minimal interface to act as an IO wrapper according to
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
this method reads all data until EOF. When EOF is reached, this method returns
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

static VALUE rRackStrIO;
static VALUE rRackFileIO;

static ID pos_id;
static ID end_id;
static ID env_id;
static ID io_id;

static VALUE TCPSOCKET_CLASS;
static ID for_fd_id;

#define set_uuid(object, request)                                              \
  rb_ivar_set((object), fd_var_id, ULONG2NUM((request)->metadata.fd))

inline static intptr_t get_uuid(VALUE obj) {
  VALUE i = rb_ivar_get(obj, fd_var_id);
  return (intptr_t)FIX2ULONG(i);
}

#define set_pos(object, pos) rb_ivar_set((object), pos_id, ULONG2NUM(pos))

inline static size_t get_pos(VALUE obj) {
  VALUE i = rb_ivar_get(obj, pos_id);
  return (size_t)FIX2ULONG(i);
}

inline static size_t get_end(VALUE obj) {
  VALUE i = rb_ivar_get(obj, end_id);
  return (size_t)FIX2ULONG(i);
}

/* *****************************************************************************
StrIO API
*/

// a macro helper to get the server pointer embeded in an object
inline static char *get_str(VALUE obj) {
  VALUE i = rb_ivar_get(obj, io_id);
  return (char *)FIX2ULONG(i);
}

/**
Gets returns a line. this is okay for small lines,
but shouldn't really be used.

Limited to ~ 1Mb of a line length.
*/
static VALUE strio_gets(VALUE self) {
  char *str = get_str(self);
  size_t pos = get_pos(self);
  size_t end = get_end(self);
  if (str == NULL || pos == end)
    return Qnil;
  size_t pos_e = pos;

  while ((pos_e < end) && str[pos_e] != '\n')
    pos_e++;
  set_pos(self, pos_e + 1);
  return rb_enc_str_new(str + pos, pos_e - pos, BinaryEncoding);
}

// Reads data from the IO, according to the Rack specifications for `#read`.
static VALUE strio_read(int argc, VALUE *argv, VALUE self) {
  char *str = get_str(self);
  size_t pos = get_pos(self);
  size_t end = get_end(self);
  VALUE buffer = Qnil;
  char ret_nil = 0;
  ssize_t len = 0;
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
    ret_nil = 1;
  }
  // return if we're at the EOF.
  if (str == NULL)
    goto no_data;
  // calculate length if it wasn't specified.
  if (len == 0) {
    // make sure we're not reading more then we have (string buffer)
    len = end - pos;
    // set position for future reads
    set_pos(self, end);
    if (len == 0)
      goto no_data;
  } else {
    // set position for future reads
    set_pos(self, pos + len);
  }
  if (len + pos > end)
    len = end - pos;
  // create the buffer if we don't have one.
  if (buffer == Qnil) {
    buffer = rb_str_buf_new(len);
    // make sure the buffer is binary encoded.
    rb_enc_associate(buffer, BinaryEncoding);
  } else {
    // make sure the buffer is binary encoded.
    rb_enc_associate(buffer, BinaryEncoding);
    if (rb_str_capacity(buffer) < len)
      rb_str_resize(buffer, len);
  }
  // read the data.
  memcpy(RSTRING_PTR(buffer), str + pos, len);
  rb_str_set_len(buffer, len);
  return buffer;
no_data:
  if (ret_nil)
    return Qnil;
  else
    return rb_str_buf_new(0);
}

// Does nothing - this is controlled by the server.
static VALUE strio_close(VALUE self) { return Qnil; }

// Rewinds the IO, so that it is read from the begining.
static VALUE rio_rewind(VALUE self) {
  set_pos(self, 0);
  return self;
}

// Passes each line of the input to the block. This should be avoided.
static VALUE strio_each(VALUE self) {
  rb_need_block();
  rio_rewind(self);
  VALUE str = Qnil;
  while ((str = strio_gets(self)) != Qnil) {
    rb_yield(str);
  }
  return self;
}

/* *****************************************************************************
TempFileIO API
*/

// a macro helper to get the server pointer embeded in an object
inline static int get_tmpfile(VALUE obj) {
  VALUE i = rb_ivar_get(obj, io_id);
  return (int)FIX2INT(i);
}

/**
Gets returns a line. this is okay for small lines,
but shouldn't really be used.

Limited to ~ 1Mb of a line length.
*/
static VALUE tfio_gets(VALUE self) {
  int fd = get_tmpfile(self);
  size_t pos = get_pos(self);
  size_t end = get_end(self);
  if (pos == end)
    return Qnil;
  size_t pos_e = pos;
  char c;
  int ret;
  VALUE buffer;

  do {
    ret = pread(fd, &c, 1, pos_e);
  } while (ret > 0 && c != '\n' && (++pos_e < end));
  set_pos(self, pos_e + 1);
  if (pos > pos_e) {
    buffer = rb_str_buf_new(pos_e - pos);
    // make sure the buffer is binary encoded.
    rb_enc_associate(buffer, BinaryEncoding);
    if (pread(fd, RSTRING_PTR(buffer), pos_e - pos, pos) < 0)
      return Qnil;
    rb_str_set_len(buffer, pos_e - pos);
    return buffer;
  }
  return Qnil;
}

// Reads data from the IO, according to the Rack specifications for `#read`.
static VALUE tfio_read(int argc, VALUE *argv, VALUE self) {
  int fd = get_tmpfile(self);
  size_t pos = get_pos(self);
  size_t end = get_end(self);
  VALUE buffer = Qnil;
  char ret_nil = 0;
  ssize_t len = 0;
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
    ret_nil = 1;
  }
  // return if we're at the EOF.
  if (pos == end)
    goto no_data;
  // calculate length if it wasn't specified.
  if (len == 0) {
    // make sure we're not reading more then we have
    len = end - pos;
    // set position for future reads
    set_pos(self, end);
    if (len == 0)
      goto no_data;
  } else {
    // set position for future reads
    set_pos(self, pos + len);
  }
  // limit read to what we have
  if (len + pos > end)
    len = end - pos;
  // create the buffer if we don't have one.
  if (buffer == Qnil) {
    buffer = rb_str_buf_new(len);
    // make sure the buffer is binary encoded.
    rb_enc_associate(buffer, BinaryEncoding);
  } else {
    // make sure the buffer is binary encoded.
    rb_enc_associate(buffer, BinaryEncoding);
    if (rb_str_capacity(buffer) < len)
      rb_str_resize(buffer, len);
  }
  // read the data.
  if (pread(fd, RSTRING_PTR(buffer), len, pos) <= 0)
    goto no_data;
  rb_str_set_len(buffer, len);
  return buffer;
no_data:
  if (ret_nil)
    return Qnil;
  else
    return rb_str_buf_new(0);
}

// Does nothing - this is controlled by the server.
static VALUE tfio_close(VALUE self) { return Qnil; }

// Passes each line of the input to the block. This should be avoided.
static VALUE tfio_each(VALUE self) {
  rb_need_block();
  rio_rewind(self);
  VALUE str = Qnil;
  while ((str = tfio_gets(self)) != Qnil) {
    rb_yield(str);
  }
  return self;
}

/* *****************************************************************************
Hijacking
*/

// defined by iodine_http
extern VALUE R_HIJACK;    // for Rack: rack.hijack
extern VALUE R_HIJACK_CB; // for Rack: rack.hijack
extern VALUE R_HIJACK_IO; // for Rack: rack.hijack_io

static VALUE rio_get_io(int argc, VALUE *argv, VALUE self) {
  if (TCPSOCKET_CLASS == Qnil)
    return Qfalse;
  intptr_t fduuid = get_uuid(self);
  // hijack the IO object
  VALUE fd = INT2FIX(sock_uuid2fd(fduuid));
  VALUE env = rb_ivar_get(self, env_id);
  // make sure we're not repeating ourselves
  VALUE new_io = rb_hash_aref(env, R_HIJACK_IO);
  if (new_io != Qnil)
    return new_io;
  // VALUE new_io = how the fuck do we create a new IO from the fd?
  new_io = RubyCaller.call2(TCPSOCKET_CLASS, for_fd_id, 1,
                            &fd); // TCPSocket.for_fd(fd) ... cool...
  rb_hash_aset(env, R_HIJACK_IO, new_io);
  if (argc)
    rb_hash_aset(env, R_HIJACK_CB, *argv);
  return new_io;
}

/* *****************************************************************************
C land API
*/

// new object
static VALUE new_rack_io(http_request_s *request, VALUE env) {
  VALUE rack_io;
  if (request->body_file > 0) {
    rack_io = rb_funcall2(rRackFileIO, new_func_id, 0, NULL);
    rb_ivar_set(rack_io, io_id, ULONG2NUM(request->body_file));
    lseek(request->body_file, 0, SEEK_SET);
  } else {
    rack_io = rb_funcall2(rRackStrIO, new_func_id, 0, NULL);
    rb_ivar_set(rack_io, io_id, ULONG2NUM(((intptr_t)request->body_str)));
    // fprintf(stderr, "rack body IO (%lu, %p):%.*s\n", request->content_length,
    //         request->body_str, (int)request->content_length,
    //         request->body_str);
  }
  set_uuid(rack_io, request);
  set_pos(rack_io, 0);
  rb_ivar_set(rack_io, end_id, ULONG2NUM(request->content_length));
  rb_ivar_set(rack_io, env_id, env);

  return rack_io;
}

// initialize library
static void init_rack_io(void) {
  rRackStrIO = rb_define_class_under(IodineBase, "RackStrIO", rb_cObject);
  rRackFileIO = rb_define_class_under(IodineBase, "RackTmpFileIO", rb_cObject);

  pos_id = rb_intern("pos");
  end_id = rb_intern("io_end");
  io_id = rb_intern("rack_io");
  env_id = rb_intern("env");
  for_fd_id = rb_intern("for_fd");

  TCPSOCKET_CLASS = rb_const_get(rb_cObject, rb_intern("TCPSocket"));
  // IO methods
  rb_define_method(rRackStrIO, "rewind", rio_rewind, 0);
  rb_define_method(rRackStrIO, "gets", strio_gets, 0);
  rb_define_method(rRackStrIO, "read", strio_read, -1);
  rb_define_method(rRackStrIO, "close", strio_close, 0);
  rb_define_method(rRackStrIO, "each", strio_each, 0);
  rb_define_method(rRackStrIO, "_hijack", rio_get_io, -1);

  rb_define_method(rRackFileIO, "rewind", rio_rewind, 0);
  rb_define_method(rRackFileIO, "gets", tfio_gets, 0);
  rb_define_method(rRackFileIO, "read", tfio_read, -1);
  rb_define_method(rRackFileIO, "close", tfio_close, 0);
  rb_define_method(rRackFileIO, "each", tfio_each, 0);
  rb_define_method(rRackFileIO, "_hijack", rio_get_io, -1);
}

////////////////////////////////////////////////////////////////////////////
// the API interface
struct _RackIO_ RackIO = {
    .new = new_rack_io, .init = init_rack_io,
};
