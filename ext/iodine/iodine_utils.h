#ifndef H___IODINE_UTILS___H
#define H___IODINE_UTILS___H
#include "iodine.h"

/* *****************************************************************************
URL encoding Helpers
***************************************************************************** */

/** Decodes percent encoding, including the `%uxxxx` javascript extension and
 * converting `+` to spaces. */
FIO_IFUNC VALUE iodine_utils_encode_with_encoding(
    int argc,
    VALUE *argv,
    VALUE self,
    int (*writer)(fio_str_info_s *dest,
                  fio_string_realloc_fn reallocate,
                  const void *encoded,
                  size_t len)) {
  if (!argc || argc > 2)
    rb_raise(rb_eArgError, "Wrong number of arguments (%d)", argc);
  rb_check_type(argv[0], RUBY_T_STRING);
  if (!RSTRING_LEN(argv[0]))
    return argv[0];
  rb_encoding *enc = NULL;
  if (argc == 2)
    enc = (TYPE(argv[1]) == T_STRING) ? rb_enc_find(RSTRING_PTR(argv[1]))
                                      : rb_enc_get(argv[1]);
  if (!enc)
    enc = IodineUTF8Encoding;
  FIO_STR_INFO_TMP_VAR(tmp, 512);
  char *org = tmp.buf;
  writer(&tmp,
         FIO_STRING_ALLOC_COPY,
         RSTRING_PTR(argv[0]),
         RSTRING_LEN(argv[0]));
  self = rb_str_new(tmp.buf, tmp.len);
  rb_enc_associate(self, enc);
  if (org != tmp.buf)
    FIO_STRING_FREE2(tmp);
  return self;
}

FIO_IFUNC VALUE
iodine_utils_encode_internal(VALUE mod,
                             VALUE arg,
                             int (*writer)(fio_str_info_s *dest,
                                           fio_string_realloc_fn reallocate,
                                           const void *encoded,
                                           size_t len)) {
  rb_check_type(arg, RUBY_T_STRING);
  if (!RSTRING_LEN(arg))
    return arg;
  FIO_STR_INFO_TMP_VAR(tmp, 512);
  const char *org = tmp.buf;
  writer(&tmp, FIO_STRING_ALLOC_COPY, RSTRING_PTR(arg), RSTRING_LEN(arg));
  arg = rb_str_new(tmp.buf, tmp.len);
  rb_enc_associate(arg, IodineUTF8Encoding);
  if (org != tmp.buf)
    FIO_STRING_FREE2(tmp);
  return arg;
}
FIO_IFUNC VALUE
iodine_utils_encode1_internal(VALUE mod,
                              VALUE arg,
                              int (*writer)(fio_str_info_s *dest,
                                            fio_string_realloc_fn reallocate,
                                            const void *encoded,
                                            size_t len)) {
  rb_check_type(arg, RUBY_T_STRING);
  if (!RSTRING_LEN(arg))
    return arg;
  FIO_STR_INFO_TMP_VAR(tmp, 512);
  const char *org = tmp.buf;
  writer(&tmp, FIO_STRING_ALLOC_COPY, RSTRING_PTR(arg), RSTRING_LEN(arg));
  rb_str_set_len(arg, 0);
  rb_str_cat(arg, tmp.buf, tmp.len);
  rb_enc_associate(arg, IodineUTF8Encoding);
  if (org != tmp.buf)
    FIO_STRING_FREE2(tmp);
  return arg;
}

/** Encodes a String using percent encoding (i.e., URI encoding). */
FIO_SFUNC VALUE iodine_utils_encode_url(VALUE mod, VALUE arg) {
  return iodine_utils_encode_internal(mod, arg, fio_string_write_url_enc);
}

/** Encodes a String using percent encoding (i.e., URI encoding). */
FIO_SFUNC VALUE iodine_utils_encode_url1(VALUE mod, VALUE arg) {
  return iodine_utils_encode1_internal(mod, arg, fio_string_write_url_enc);
}

/** Encodes a String using percent encoding (i.e., URI encoding). */
FIO_SFUNC VALUE iodine_utils_encode_path(VALUE mod, VALUE arg) {
  return iodine_utils_encode_internal(mod, arg, fio_string_write_url_enc);
}

/** Encodes a String in place using percent encoding (i.e., URI encoding). */
FIO_SFUNC VALUE iodine_utils_encode_path1(VALUE mod, VALUE arg) {
  return iodine_utils_encode1_internal(mod, arg, fio_string_write_url_enc);
}

/** Decodes percent encoding, including the `%uxxxx` javascript extension and
 * converting `+` to spaces. */
FIO_SFUNC VALUE iodine_utils_decode_url(int argc, VALUE *argv, VALUE self) {
  return iodine_utils_encode_with_encoding(argc,
                                           argv,
                                           self,
                                           fio_string_write_url_dec);
}

/** Decodes percent encoding in place, including the `%uxxxx` javascript
 * extension and converting `+` to spaces. */
FIO_SFUNC VALUE iodine_utils_decode_url1(VALUE mod, VALUE arg) {
  return iodine_utils_encode1_internal(mod, arg, fio_string_write_url_dec);
}

/** Decodes percent encoding, including the `%uxxxx` javascript extension. */
FIO_SFUNC VALUE iodine_utils_decode_path(int argc, VALUE *argv, VALUE self) {
  return iodine_utils_encode_with_encoding(argc,
                                           argv,
                                           self,
                                           fio_string_write_path_dec);
}

/** Decodes percent encoding in place, including the `%uxxxx` javascript. */
FIO_SFUNC VALUE iodine_utils_decode_path1(VALUE mod, VALUE arg) {
  return iodine_utils_encode1_internal(mod, arg, fio_string_write_path_dec);
}

/** Escapes String using HTML escape encoding. */
FIO_SFUNC VALUE iodine_utils_encode_html(VALUE mod, VALUE arg) {
  return iodine_utils_encode_internal(mod, arg, fio_string_write_html_escape);
}

/**
 * Escapes String in place using HTML escape encoding.
 *
 * Note: this function significantly increases the number of escaped characters
 * when compared to the native implementation.
 */
FIO_SFUNC VALUE iodine_utils_encode_html1(VALUE mod, VALUE arg) {
  return iodine_utils_encode1_internal(mod, arg, fio_string_write_html_escape);
}

/** Decodes an HTML escaped String. */
FIO_SFUNC VALUE iodine_utils_decode_html(int argc, VALUE *argv, VALUE self) {
  return iodine_utils_encode_with_encoding(argc,
                                           argv,
                                           self,
                                           fio_string_write_html_unescape);
}

/** Decodes an HTML escaped String in place. */
FIO_SFUNC VALUE iodine_utils_decode_html1(VALUE mod, VALUE arg) {
  return iodine_utils_encode1_internal(mod,
                                       arg,
                                       fio_string_write_html_unescape);
}

/* *****************************************************************************
Time to String Helpers
***************************************************************************** */

FIO_IFUNC time_t iodine_utils_rb2time(VALUE rtm) {
  rtm = rb_funcallv(rtm, rb_intern("to_i"), 0, NULL);
  return FIX2LONG(rtm) ? FIX2LONG(rtm) : fio_time_real().tv_sec;
}

/** Takes a Time object and returns a String conforming to RFC 2109. */
FIO_SFUNC VALUE iodine_utils_rfc2109(VALUE mod, VALUE rtm) {
  time_t time_requested = iodine_utils_rb2time(rtm);
  VALUE str = rb_str_buf_new(34);
  rb_str_set_len(str, fio_time2rfc2109(RSTRING_PTR(str), time_requested));
  rb_enc_associate(str, IodineUTF8Encoding);
  return str;
  (void)mod;
}
/** Takes a Time object and returns a String conforming to RFC 2822. */
FIO_SFUNC VALUE iodine_utils_rfc2822(VALUE mod, VALUE rtm) {
  time_t time_requested = iodine_utils_rb2time(rtm);
  VALUE str = rb_str_buf_new(34);
  rb_str_set_len(str, fio_time2rfc2822(RSTRING_PTR(str), time_requested));
  rb_enc_associate(str, IodineUTF8Encoding);
  return str;
  (void)mod;
}
/** Takes a Time object and returns a String conforming to RFC 7231. */
FIO_SFUNC VALUE iodine_utils_rfc7231(VALUE mod, VALUE rtm) {
  time_t time_requested = iodine_utils_rb2time(rtm);
  VALUE str = rb_str_buf_new(34);
  rb_str_set_len(str, fio_time2rfc7231(RSTRING_PTR(str), time_requested));
  rb_enc_associate(str, IodineUTF8Encoding);
  return str;
  (void)mod;
}

/* *****************************************************************************
String Secure Compare
***************************************************************************** */
// clang-format off
/**
Securely compares two String objects to see if they are equal.

Designed to be secure against timing attacks when both String objects are of the same length.

                require 'iodine'
                require 'rack'
                require 'benchmark'
                def prove_secure_compare(name, mthd, length = 4096)
                  a = 0;
                  b = 0;
                  str1 = Array.new(length) { 'a' } .join; str2 = Array.new(length) { 'a' } .join;
                  bm = Benchmark.measure do
                    1024.times do
                      tmp = Benchmark.measure {4096.times {mthd.call(str1, str2)}}
                      str1[0] = 'b'
                      tmp2 = Benchmark.measure {4096.times {mthd.call(str1, str2)}}
                      str1[0] = 'a'
                      tmp = tmp2.total - tmp.total
                      a += 1 if tmp >= 0
                      b += 1 if tmp <= 0
                    end
                  end
                  puts "#{name} timing ratio #{a}:#{b}\n#{bm.to_s}\n"
                end
                prove_secure_compare("String == (short string)", (Proc.new {|a,b| a == b } ), 47);
                prove_secure_compare("Iodine::Utils.secure_compare (short string)", Iodine::Utils.method(:secure_compare), 47)
                prove_secure_compare("Rack::Utils.secure_compare (short string)", Rack::Utils.method(:secure_compare), 47)
                prove_secure_compare("String == (long string)", (Proc.new {|a,b| a == b } ), 1024)
                prove_secure_compare("Iodine::Utils.secure_compare (long string)", Iodine::Utils.method(:secure_compare), 1024)
                # prove_secure_compare("Rack::Utils.secure_compare (short string)", Rack::Utils.method(:secure_compare), 1024) # VERY slow

*/
FIO_SFUNC VALUE iodine_utils_is_eq(VALUE mod, VALUE a, VALUE b) { // clang-format on
  rb_check_type(a, RUBY_T_STRING);
  rb_check_type(b, RUBY_T_STRING);
  if (RSTRING_LEN(a) != RSTRING_LEN(b))
    return RUBY_Qfalse;
  return fio_ct_is_eq(RSTRING_PTR(a), RSTRING_PTR(b), RSTRING_LEN(a))
             ? RUBY_Qtrue
             : RUBY_Qfalse;
}

/* *****************************************************************************
Create Methods in Module
***************************************************************************** */

FIO_SFUNC void iodine_utils_define_methods(VALUE m) {
  rb_define_singleton_method(m, "escape_path", iodine_utils_encode_path, 1);
  rb_define_singleton_method(m, "escape_path!", iodine_utils_encode_path1, 1);
  rb_define_singleton_method(m, "unescape_path", iodine_utils_decode_path, -1);
  rb_define_singleton_method(m, "unescape_path!", iodine_utils_decode_path1, 1);
  rb_define_singleton_method(m, "escape", iodine_utils_encode_url, 1);
  rb_define_singleton_method(m, "escape!", iodine_utils_encode_url1, 1);
  rb_define_singleton_method(m, "unescape", iodine_utils_decode_url, -1);
  rb_define_singleton_method(m, "unescape!", iodine_utils_decode_url1, 1);
  rb_define_singleton_method(m, "escape_html", iodine_utils_encode_html, 1);
  rb_define_singleton_method(m, "escape_html!", iodine_utils_encode_html1, 1);
  rb_define_singleton_method(m, "unescape_html", iodine_utils_decode_html, -1);
  rb_define_singleton_method(m, "unescape_html!", iodine_utils_decode_html1, 1);
  rb_define_singleton_method(m, "rfc2109", iodine_utils_rfc2109, 1);
  rb_define_singleton_method(m, "rfc2822", iodine_utils_rfc2822, 1);
  rb_define_singleton_method(m, "time2str", iodine_utils_rfc7231, 1);
  rb_define_singleton_method(m, "secure_compare", iodine_utils_is_eq, 2);
}

/**
 * Adds the `Iodine::Utils` methods to the modules passed as arguments.
 *
 * If no modules were passed to the `monkey_patch` method, `Rack::Utils` will be
 * monkey patched.
 *
 */
FIO_SFUNC VALUE iodine_utils_monkey_patch(int argc, VALUE *argv, VALUE self) {
  VALUE default_module;
  if (!argc) {
    argc = 1;
    argv = &default_module;
    default_module = rb_define_module_under(rb_define_module("Rack"), "Utils");
  }
  for (int i = 0; i < argc; ++i) {
    rb_check_type(argv[i], T_MODULE);
    iodine_utils_define_methods(argv[i]);
  }
  return self;
}

/** Initialize Iodine::Utils */ // clang-format off
/**

# Utility Helpers - Iodine's Helpers

These are some unescaping / decoding helpers provided by Iodine.

These **should** be faster then their common Ruby / Rack alternative.

Performance may differ according to architecture and compiler used. Please measure:

        require 'iodine'
        require 'rack'
        require 'cgi'
        require 'benchmark/ips'
        encoded = '%E3 + %83 + %AB + %E3 + %83 + %93 + %E3 + %82 + %A4 + %E3 + %82 + %B9 + %E3 + %81 + %A8' # a String in need of decoding
        decoded = Rack::Utils.unescape(encoded, "binary")
        html_xss = "<script>alert('avoid xss attacks')</script>"
        html_xss_safe = Rack::Utils.escape_html html_xss
        short_str1 = Array.new(64) { 'a' } .join
        short_str2 = Array.new(64) { 'a' } .join
        long_str1 = Array.new(4094) { 'a' } .join
        long_str2 = Array.new(4094) { 'a' } .join
        now_preclaculated = Time.now
        Benchmark.ips do |bm|
            bm.report(" Iodine rfc2822")    { Iodine::Utils.rfc2822(now_preclaculated) }
            bm.report("   Rack rfc2822")    {   Rack::Utils.rfc2822(now_preclaculated) }
          bm.compare!
        end; Benchmark.ips do |bm|
            bm.report("Iodine unescape")    { Iodine::Utils.unescape encoded }
            bm.report("  Rack unescape")    {   Rack::Utils.unescape encoded }
          bm.compare!
        end; Benchmark.ips do |bm|
            bm.report("Iodine escape")    { Iodine::Utils.escape decoded }
            bm.report("  Rack escape")    {   Rack::Utils.escape decoded }
          bm.compare!
        end; Benchmark.ips do |bm|
            bm.report("Iodine escape HTML")    { Iodine::Utils.escape_html html_xss }
            bm.report("  Rack escape HTML")    {   Rack::Utils.escape_html html_xss }
          bm.compare!
        end; Benchmark.ips do |bm|
            bm.report("Iodine unescape HTML")    { Iodine::Utils.unescape_html html_xss_safe }
            bm.report("   CGI unescape HTML")    {   CGI.unescapeHTML html_xss_safe }
          bm.compare!
        end; Benchmark.ips do |bm|
            bm.report("Iodine secure compare (short)")    { Iodine::Utils.secure_compare short_str1, short_str2 }
            bm.report("  Rack secure compare (short)")    {   Rack::Utils.secure_compare short_str1, short_str2 }
          bm.compare!
        end; Benchmark.ips do |bm|
            bm.report("Iodine secure compare (long)")    { Iodine::Utils.secure_compare long_str1, long_str2 }
            bm.report("  Rack secure compare (long)")    {   Rack::Utils.secure_compare long_str1, long_str2 }
          bm.compare!
        end && nil

 */
static void Init_Iodine_Utils(void) {
  VALUE m = rb_define_module_under(iodine_rb_IODINE, "Utils");
  rb_define_singleton_method(m, "escape_path", iodine_utils_encode_path, 1);
  rb_define_singleton_method(m, "escape_path!", iodine_utils_encode_path1, 1);
  rb_define_singleton_method(m, "unescape_path", iodine_utils_decode_path, -1);
  rb_define_singleton_method(m, "unescape_path!", iodine_utils_decode_path1, 1);
  rb_define_singleton_method(m, "escape", iodine_utils_encode_url, 1);
  rb_define_singleton_method(m, "escape!", iodine_utils_encode_url1, 1);
  rb_define_singleton_method(m, "unescape", iodine_utils_decode_url, -1);
  rb_define_singleton_method(m, "unescape!", iodine_utils_decode_url1, 1);
  rb_define_singleton_method(m, "escape_html", iodine_utils_encode_html, 1);
  rb_define_singleton_method(m, "escape_html!", iodine_utils_encode_html1, 1);
  rb_define_singleton_method(m, "unescape_html", iodine_utils_decode_html, -1);
  rb_define_singleton_method(m, "unescape_html!", iodine_utils_decode_html1, 1);
  rb_define_singleton_method(m, "rfc2109", iodine_utils_rfc2109, 1);
  rb_define_singleton_method(m, "rfc2822", iodine_utils_rfc2822, 1);
  rb_define_singleton_method(m, "time2str", iodine_utils_rfc7231, 1);
  rb_define_singleton_method(m, "secure_compare", iodine_utils_is_eq, 2);
  rb_define_singleton_method(m, "monkey_patch", iodine_utils_monkey_patch, -1);
}      // clang-format on
#endif /* H___IODINE_UTILS___H */
