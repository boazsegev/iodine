/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine.h"

#include "http.h"
#include <ruby/encoding.h>

/*
Add all sorts of useless stuff here.
*/

static ID iodine_to_i_func_id;
static rb_encoding *IodineUTF8Encoding;

/* *****************************************************************************
URL Decoding
***************************************************************************** */
/**
Decodes a URL encoded String in place.

Raises an exception on error... but this might result in a partially decoded
String.
*/
static VALUE url_decode_inplace(VALUE self, VALUE str) {
  Check_Type(str, T_STRING);
  ssize_t len =
      http_decode_url(RSTRING_PTR(str), RSTRING_PTR(str), RSTRING_LEN(str));
  if (len < 0)
    rb_raise(rb_eRuntimeError, "Malformed URL string - couldn't decode (String "
                               "might have been partially altered).");
  rb_str_set_len(str, len);
  return str;
  (void)self;
}

/**
Decodes a URL encoded String, returning a new String with the decoded data.
*/
static VALUE url_decode(VALUE self, VALUE str) {
  Check_Type(str, T_STRING);
  VALUE str2 = rb_str_buf_new(RSTRING_LEN(str));
  ssize_t len =
      http_decode_url(RSTRING_PTR(str2), RSTRING_PTR(str), RSTRING_LEN(str));
  if (len < 0)
    rb_raise(rb_eRuntimeError, "Malformed URL string - couldn't decode.");
  rb_str_set_len(str2, len);
  return str2;
  (void)self;
}

/**
Decodes a percent encoded String (normally the "path" of a request), editing the
String in place.

Raises an exception on error... but this might result in a partially decoded
String.
*/
static VALUE path_decode_inplace(VALUE self, VALUE str) {
  Check_Type(str, T_STRING);
  ssize_t len =
      http_decode_path(RSTRING_PTR(str), RSTRING_PTR(str), RSTRING_LEN(str));
  if (len < 0)
    rb_raise(rb_eRuntimeError,
             "Malformed URL path string - couldn't decode (String "
             "might have been partially altered).");
  rb_str_set_len(str, len);
  return str;
  (void)self;
}

/**
Decodes a percent encoded String (normally the "path" of a request), returning a
new String with the decoded data.
*/
static VALUE path_decode(VALUE self, VALUE str) {
  Check_Type(str, T_STRING);
  VALUE str2 = rb_str_buf_new(RSTRING_LEN(str));
  ssize_t len =
      http_decode_path(RSTRING_PTR(str2), RSTRING_PTR(str), RSTRING_LEN(str));
  if (len < 0)
    rb_raise(rb_eRuntimeError, "Malformed URL path string - couldn't decode.");
  rb_str_set_len(str2, len);
  return str2;
  (void)self;
}

/**
Decodes a URL encoded String, returning a new String with the decoded data.

This variation matches the Rack::Utils.unescape signature by accepting and
mostly ignoring an optional Encoding argument.
*/
static VALUE unescape(int argc, VALUE *argv, VALUE self) {
  if (argc < 1 || argc > 2)
    rb_raise(rb_eArgError,
             "wrong number of arguments (given %d, expected 1..2).", argc);
  VALUE str = argv[0];
  Check_Type(str, T_STRING);
  VALUE str2 = rb_str_buf_new(RSTRING_LEN(str));
  ssize_t len =
      http_decode_url(RSTRING_PTR(str2), RSTRING_PTR(str), RSTRING_LEN(str));
  if (len < 0)
    rb_raise(rb_eRuntimeError, "Malformed URL path string - couldn't decode.");
  rb_str_set_len(str2, len);
  rb_encoding *enc = IodineUTF8Encoding;
  if (argc == 2 && argv[1] != Qnil && argv[1] != Qfalse) {
    enc = rb_enc_get(argv[1]);
    if (!enc)
      enc = IodineUTF8Encoding;
  }
  rb_enc_associate(str2, enc);
  return str2;
  (void)self;
}

/* *****************************************************************************
HTTP Dates
***************************************************************************** */

/**
Takes an optional Integer for Unix Time and returns a faster (though less
localized) HTTP Date formatted String.


        Iodine::Rack.time2str => "Sun, 11 Jun 2017 06:14:08 GMT"

        Iodine::Rack.time2str(Time.now.to_i) => "Wed, 15 Nov 1995 06:25:24 GMT"

Since Iodine uses time caching within it's reactor, using the default value
(now) will be faster than providing an explicit time using `Time.now.to_i`.

*/
static VALUE date_str(int argc, VALUE *argv, VALUE self) {
  if (argc > 1)
    rb_raise(rb_eArgError,
             "wrong number of arguments (given %d, expected 0..1).", argc);
  time_t last_tick;
  if (argc) {
    if (TYPE(argv[0]) != T_FIXNUM)
      argv[0] = rb_funcallv(argv[0], iodine_to_i_func_id, 0, NULL);
    Check_Type(argv[0], T_FIXNUM);
    last_tick =
        FIX2ULONG(argv[0]) ? FIX2ULONG(argv[0]) : fio_last_tick().tv_sec;
  } else
    last_tick = fio_last_tick().tv_sec;
  VALUE str = rb_str_buf_new(32);
  struct tm tm;

  http_gmtime(last_tick, &tm);
  size_t len = http_date2str(RSTRING_PTR(str), &tm);
  rb_str_set_len(str, len);
  return str;
  (void)self;
}

/**
Takes `time` and returns a faster (though less localized) HTTP Date formatted
String.


        Iodine::Rack.rfc2822(Time.now) => "Sun, 11 Jun 2017 06:14:08 -0000"

        Iodine::Rack.rfc2822(0)      => "Sun, 11 Jun 2017 06:14:08 -0000"

Since Iodine uses time caching within it's reactor, using the default value
(by passing 0) will be faster than providing an explicit time using `Time.now`.
*/
static VALUE iodine_rfc2822(VALUE self, VALUE rtm) {
  time_t last_tick;
  rtm = rb_funcallv(rtm, iodine_to_i_func_id, 0, NULL);
  last_tick = FIX2ULONG(rtm) ? FIX2ULONG(rtm) : fio_last_tick().tv_sec;
  VALUE str = rb_str_buf_new(34);
  struct tm tm;

  http_gmtime(last_tick, &tm);
  size_t len = http_date2rfc2822(RSTRING_PTR(str), &tm);
  rb_str_set_len(str, len);
  return str;
  (void)self;
}

/**
Takes `time` and returns a faster (though less localized) HTTP Date formatted
String.


        Iodine::Rack.rfc2109(Time.now) => "Sun, 11-Jun-2017 06:14:08 GMT"

        Iodine::Rack.rfc2109(0)      => "Sun, 11-Jun-2017 06:14:08 GMT"

Since Iodine uses time caching within it's reactor, using the default value
(by passing 0) will be faster than providing an explicit time using `Time.now`.
*/
static VALUE iodine_rfc2109(VALUE self, VALUE rtm) {
  time_t last_tick;
  rtm = rb_funcallv(rtm, iodine_to_i_func_id, 0, NULL);
  last_tick = FIX2ULONG(rtm) ? FIX2ULONG(rtm) : fio_last_tick().tv_sec;
  VALUE str = rb_str_buf_new(32);
  struct tm tm;

  http_gmtime(last_tick, &tm);
  size_t len = http_date2rfc2109(RSTRING_PTR(str), &tm);
  rb_str_set_len(str, len);
  return str;
  (void)self;
}

/* *****************************************************************************
Ruby Initialization
***************************************************************************** */

void iodine_init_helpers(void) {
  iodine_to_i_func_id = rb_intern("to_i");
  IodineUTF8Encoding = rb_enc_find("UTF-8");
  VALUE tmp = rb_define_module_under(IodineModule, "Rack");
  // clang-format off
  /*
Iodine does NOT monkey patch Rack automatically. However, it's possible and recommended to moneky patch Rack::Utils to use the methods in this module.

Choosing to monkey patch Rack::Utils could offer significant performance gains for some applications. i.e. (on my machine):

      require 'iodine'
      require 'rack'
      # a String in need of decoding
      s = '%E3%83%AB%E3%83%93%E3%82%A4%E3%82%B9%E3%81%A8'
      Benchmark.bm do |bm|
        # Pre-Patch
        bm.report("   Rack.unescape")    {1_000_000.times { Rack::Utils.unescape s } }
        bm.report("    Rack.rfc2822")    {1_000_000.times { Rack::Utils.rfc2822(Time.now) } }
        bm.report("    Rack.rfc2109")    {1_000_000.times { Rack::Utils.rfc2109(Time.now) } }
        # Perform Patch
        Iodine.patch_rack
        puts "            --- Monkey Patching Rack ---"
        # Post Patch
        bm.report("Patched.unescape")    {1_000_000.times { Rack::Utils.unescape s } }
        bm.report(" Patched.rfc2822")    {1_000_000.times { Rack::Utils.rfc2822(Time.now) } }
        bm.report(" Patched.rfc2109")    {1_000_000.times { Rack::Utils.rfc2109(Time.now) } }
      end && nil

Results:
        user     system      total        real
        Rack.unescape  8.706881   0.019995   8.726876 (  8.740530)
        Rack.rfc2822  3.270305   0.007519   3.277824 (  3.279416)
        Rack.rfc2109  3.152188   0.003852   3.156040 (  3.157975)
                   --- Monkey Patching Rack ---
        Patched.unescape  0.327231   0.003125   0.330356 (  0.337090)
        Patched.rfc2822  0.691304   0.003330   0.694634 (  0.701172)
        Patched.rfc2109  0.685029   0.001956   0.686985 (  0.687607)

  */
  tmp = rb_define_module_under(tmp, "Utils");
  // clang-format on
  rb_define_module_function(tmp, "decode_url!", url_decode_inplace, 1);
  rb_define_module_function(tmp, "decode_url", url_decode, 1);
  rb_define_module_function(tmp, "decode_path!", path_decode_inplace, 1);
  rb_define_module_function(tmp, "decode_path", path_decode, 1);
  rb_define_module_function(tmp, "time2str", date_str, -1);
  rb_define_module_function(tmp, "rfc2109", iodine_rfc2109, 1);
  rb_define_module_function(tmp, "rfc2822", iodine_rfc2822, 1);

  /*
The monkey-patched methods are in this module, allowing Iodine::Rack::Utils to
include non-patched methods as well.
  */
  tmp = rb_define_module_under(IodineBaseModule, "MonkeyPatch");
  tmp = rb_define_module_under(tmp, "RackUtils");
  // clang-format on
  /* we define it all twice for easier monkey patching */
  rb_define_method(tmp, "unescape", unescape, -1);
  rb_define_method(tmp, "unescape_path", path_decode, 1);
  rb_define_method(tmp, "rfc2109", iodine_rfc2109, 1);
  rb_define_method(tmp, "rfc2822", iodine_rfc2822, 1);
  rb_define_singleton_method(tmp, "unescape", unescape, -1);
  rb_define_singleton_method(tmp, "unescape_path", path_decode, 1);
  rb_define_singleton_method(tmp, "rfc2109", iodine_rfc2109, 1);
  rb_define_singleton_method(tmp, "rfc2822", iodine_rfc2822, 1);
  // rb_define_module_function(IodineUtils, "time2str", date_str, -1);
}
