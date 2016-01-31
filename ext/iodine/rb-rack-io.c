#include "rb-rack-io.h"
#include "rb-call.h"
#include "iodine.h"
#include <ruby/encoding.h>

/* this file manages a minimal interface to act as an IO wrapper according to
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
static VALUE rRackIO;
static VALUE rRequestData;
static ID call_new_id;
static ID pos_id;
//////////////////////////////
// the request data type

// var ID
static ID req_var_id;

// GC will call this to "free" the memory... which would be bad, as it's managed
// by the server
static void dont_free(void* obj) {}

// the data wrapper and the dont_free instruction callback
static struct rb_data_type_struct http_request_data_type = {
    .wrap_struct_name = "HttpRequestData",
    .function.dfree = (void (*)(void*))dont_free,
};

// a macro helper function to embed a server pointer in an object
#define set_request(object, request) \
  rb_ivar_set(                       \
      (object), req_var_id,          \
      TypedData_Wrap_Struct(rRequestData, &http_request_data_type, (request)))

// a macro helper to get the server pointer embeded in an object
#define get_request(object) \
  ((struct HttpRequest*)DATA_PTR(rb_ivar_get((object), req_var_id)))

//////////////////////////////
// Ruby land API

// gets returns a line. this is okay for small lines, but should really be used.
static VALUE rio_gets(VALUE self) {
  struct HttpRequest* request = get_request(self);
  if (request->body_str) {
    size_t pos = FIX2LONG(rb_ivar_get(self, pos_id));
    if (pos == request->content_length)
      return Qnil;
    size_t pos_e = pos;
    while ((pos_e < request->content_length) &&
           request->body_str[pos_e] != '\n')
      pos_e++;
    rb_ivar_set(self, pos_id, LONG2FIX(pos_e + 1));
    return rb_enc_str_new(request->body_str + pos, pos_e - pos, BinaryEncoding);
  } else if (request->body_file) {
    if (feof(request->body_file))
      return Qnil;
    char* line = NULL;
    // Brrr... double copy - this sucks.
    size_t line_len = getline(&line, 0, request->body_file);
    VALUE str = rb_enc_str_new(line, line_len, BinaryEncoding);
    free(line);
    return str;
  }
  return Qnil;
}

// Reads data from the IO, according to the Rack specifications for `#read`.
static VALUE rio_read(int argc, VALUE* argv, VALUE self) {
  struct HttpRequest* request = get_request(self);
  size_t pos = FIX2LONG(rb_ivar_get(self, pos_id));
  VALUE buffer = 0;
  char ret_nil = 0;
  size_t len = 0;
  // get the buffer object if given
  if (argc == 2) {
    Check_Type(argv[1], T_STRING);
    buffer = argv[1];
  }
  // get the length object, if given
  if (argc > 0 && argv[0] != Qnil) {
    Check_Type(argv[0], T_FIXNUM);
    len = FIX2LONG(argv[0]);
    ret_nil = 1;
  }
  // return if we're at the EOF.
  if ((!request->body_str && !request->body_file) ||
      (request->body_str && (request->content_length == pos)) ||
      (request->body_file && feof(request->body_file))) {
    if (ret_nil)
      return Qnil;
    else
      return rb_str_buf_new(1);
  }
  // calculate length if it wasn't specified.
  if (!len) {
    if (request->body_file)
      pos = ftell(request->body_file);
    len = request->content_length - pos;
  } else {
    // make sure we're not reading more then we have (string buffer)
    if (request->body_str && (len > (request->content_length - pos)))
      len = request->content_length - pos;
  }
  // create the buffer if we don't have one.
  if (!buffer) {
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
  if (request->body_str) {
    memcpy(RSTRING_PTR(buffer), request->body_str + pos, len);
    rb_str_set_len(buffer, len);
    rb_ivar_set(self, pos_id, LONG2FIX(pos + len));
    return buffer;
  } else if (request->body_file) {
    if ((len = fread(RSTRING_PTR(buffer), 1, len, request->body_file)) == 0 &&
        ret_nil)
      return Qnil;
    rb_str_set_len(buffer, len);
    return buffer;
  }
  return Qnil;
}

// Does nothing - this is controlled by the server.
static VALUE rio_close(VALUE self) {
  return Qnil;
}

// Rewinds the IO, so that it is read from the begining.
static VALUE rio_rewind(VALUE self) {
  struct HttpRequest* request = get_request(self);
  if (request->body_str) {
    rb_ivar_set(self, pos_id, INT2FIX(0));
  } else if (request->body_file) {
    rewind(request->body_file);
  }
  return self;
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

//////////////////////////////
// C land API

// new object
static VALUE new_rack_io(struct HttpRequest* request) {
  VALUE rack_io = rb_funcall2(rRackIO, call_new_id, 0, NULL);
  set_request(rack_io, request);
  rb_ivar_set(rack_io, pos_id, INT2FIX(0));
  if (request->body_file)
    rewind(request->body_file);
  return rack_io;
}

// initialize library
static void init_rack_io(void) {
  rRackIO = rb_define_class_under(rBase, "RackIO", rb_cObject);
  rRequestData = rb_define_class_under(rRackIO, "RequestData", rb_cData);
  req_var_id = rb_intern("request");
  call_new_id = rb_intern("new");
  pos_id = rb_intern("pos");
  BinaryEncoding = rb_enc_find("binary");
  // IO methods
  rb_define_method(rRackIO, "rewind", rio_rewind, 0);
  rb_define_method(rRackIO, "gets", rio_gets, 0);
  rb_define_method(rRackIO, "read", rio_read, -1);
  rb_define_method(rRackIO, "close", rio_close, 0);
  rb_define_method(rRackIO, "each", rio_each, 0);
}

////////////////////////////////////////////////////////////////////////////
// the API interface
struct _RackIO_ RackIO = {
    .new = new_rack_io,
    .init = init_rack_io,
};
