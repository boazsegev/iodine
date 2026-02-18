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
  rtm = (rtm == Qnil ? LONG2NUM(0)
                     : rb_funcallv(rtm, rb_intern("to_i"), 0, NULL));
  return NUM2LONG(rtm) ? NUM2LONG(rtm) : fio_time_real().tv_sec;
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
Randomness and Friends
***************************************************************************** */

FIO_DEFINE_RANDOM128_FN(static, iodine_random, 10, 0)

FIO_SFUNC VALUE iodine_utils_hmac512(VALUE self, VALUE secret, VALUE massage) {
  rb_check_type(secret, RUBY_T_STRING);
  rb_check_type(massage, RUBY_T_STRING);
  fio_buf_info_s k = IODINE_RSTR_INFO(secret);
  fio_buf_info_s m = IODINE_RSTR_INFO(massage);
  fio_u512 h = fio_sha512_hmac(k.buf, k.len, m.buf, m.len);
  FIO_STR_INFO_TMP_VAR(out, 128);
  fio_string_write_base64enc(&out, NULL, h.u8, 64, 0);
  return rb_str_new(out.buf, out.len);
}

FIO_SFUNC VALUE iodine_utils_hmac256(VALUE self, VALUE secret, VALUE massage) {
  rb_check_type(secret, RUBY_T_STRING);
  rb_check_type(massage, RUBY_T_STRING);
  fio_buf_info_s k = IODINE_RSTR_INFO(secret);
  fio_buf_info_s m = IODINE_RSTR_INFO(massage);
  fio_u256 h = fio_sha256_hmac(k.buf, k.len, m.buf, m.len);
  FIO_STR_INFO_TMP_VAR(out, 64);
  fio_string_write_base64enc(&out, NULL, h.u8, 32, 0);
  return rb_str_new(out.buf, out.len);
}

FIO_SFUNC VALUE iodine_utils_sha256(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  fio_u256 h = fio_sha256(RSTRING_PTR(data), RSTRING_LEN(data));
  return rb_str_new((const char *)h.u8, 32);
}

FIO_SFUNC VALUE iodine_utils_sha512(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  fio_u512 h = fio_sha512(RSTRING_PTR(data), RSTRING_LEN(data));
  return rb_str_new((const char *)h.u8, 64);
}

FIO_SFUNC VALUE iodine_utils_sha3_256(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  uint8_t out[32];
  fio_sha3_256(out, RSTRING_PTR(data), RSTRING_LEN(data));
  return rb_str_new((const char *)out, 32);
}

FIO_SFUNC VALUE iodine_utils_sha3_512(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  uint8_t out[64];
  fio_sha3_512(out, RSTRING_PTR(data), RSTRING_LEN(data));
  return rb_str_new((const char *)out, 64);
}

FIO_SFUNC VALUE iodine_utils_sha3_224(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  uint8_t out[28];
  fio_sha3_224(out, RSTRING_PTR(data), RSTRING_LEN(data));
  return rb_str_new((const char *)out, 28);
}

FIO_SFUNC VALUE iodine_utils_sha3_384(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  uint8_t out[48];
  fio_sha3_384(out, RSTRING_PTR(data), RSTRING_LEN(data));
  return rb_str_new((const char *)out, 48);
}

/**
 * Computes SHAKE128 extendable-output function.
 *
 * @param data [String] Input data to hash
 * @param length: [Integer] Desired output length in bytes (default: 32)
 * @return [String] Binary string of specified length
 */
FIO_SFUNC VALUE iodine_utils_shake128(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t length = 32;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(length, 0, "length", 0));
  if (length < 1 || length > 0x0FFFFFFF)
    rb_raise(rb_eArgError, "length must be between 1 and 268435455");
  VALUE out = rb_str_buf_new((long)length);
  rb_str_set_len(out, (long)length);
  fio_shake128(RSTRING_PTR(out), (size_t)length, data.buf, data.len);
  return out;
  (void)self;
}

/**
 * Computes SHAKE256 extendable-output function.
 *
 * @param data [String] Input data to hash
 * @param length: [Integer] Desired output length in bytes (default: 64)
 * @return [String] Binary string of specified length
 */
FIO_SFUNC VALUE iodine_utils_shake256(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t length = 64;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(length, 0, "length", 0));
  if (length < 1 || length > 0x0FFFFFFF)
    rb_raise(rb_eArgError, "length must be between 1 and 268435455");
  VALUE out = rb_str_buf_new((long)length);
  rb_str_set_len(out, (long)length);
  fio_shake256(RSTRING_PTR(out), (size_t)length, data.buf, data.len);
  return out;
  (void)self;
}

/**
 * Computes SHA-1 hash (20 bytes).
 *
 * WARNING: SHA-1 is cryptographically broken. Use only for legacy protocols
 * that require it (e.g., WebSockets, TOTP compatibility).
 *
 * @param data [String] Input data to hash
 * @return [String] 20-byte binary hash
 */
FIO_SFUNC VALUE iodine_utils_sha1(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  fio_sha1_s h = fio_sha1(RSTRING_PTR(data), (uint64_t)RSTRING_LEN(data));
  return rb_str_new((const char *)h.digest, 20);
  (void)self;
}

/**
 * Computes CRC32 checksum (ITU-T V.42 / ISO 3309 / gzip polynomial 0xEDB88320).
 *
 * Uses a slicing-by-8 algorithm for high throughput. This is the standard CRC32
 * used by gzip, zlib, and Ethernet — NOT the Castagnoli (CRC32-C) variant.
 *
 * Supports incremental computation: pass the previous return value as
 * `initial_crc` to continue a checksum over multiple buffers.
 *
 * @param data [String] Input data to checksum
 * @param initial_crc: [Integer] Starting CRC value (default: 0)
 * @return [Integer] 32-bit CRC32 checksum
 */
FIO_SFUNC VALUE iodine_utils_crc32(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  uint32_t initial_crc = 0;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_U32(initial_crc, 0, "initial_crc", 0));
  uint32_t crc = fio_crc32(data.buf, data.len, initial_crc);
  return UINT2NUM(crc);
  (void)self;
}

/**
 * Computes facil.io Risky Hash (non-cryptographic, fast).
 *
 * @param data [String] Input data to hash
 * @param seed: [Integer] Optional seed value (default: 0)
 * @return [Integer] 64-bit hash value
 */
FIO_SFUNC VALUE iodine_utils_risky_hash(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  uint64_t seed = 0;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_U64(seed, 0, "seed", 0));
  uint64_t hash = fio_risky_hash(data.buf, data.len, seed);
  return ULL2NUM(hash);
  (void)self;
}

/**
 * Computes facil.io Risky256 Hash (non-cryptographic, 256-bit).
 *
 * @param data [String] Input data to hash
 * @return [String] 32-byte binary hash
 */
FIO_SFUNC VALUE iodine_utils_risky256(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  fio_u256 h = fio_risky256(RSTRING_PTR(data), (uint64_t)RSTRING_LEN(data));
  return rb_str_new((const char *)h.u8, 32);
  (void)self;
}

/**
 * Computes facil.io Risky512 Hash (non-cryptographic, 512-bit).
 *
 * First 256 bits are identical to risky256 (SHAKE-style extension).
 *
 * @param data [String] Input data to hash
 * @return [String] 64-byte binary hash
 */
FIO_SFUNC VALUE iodine_utils_risky512(VALUE self, VALUE data) {
  rb_check_type(data, RUBY_T_STRING);
  fio_u512 h = fio_risky512(RSTRING_PTR(data), (uint64_t)RSTRING_LEN(data));
  return rb_str_new((const char *)h.u8, 64);
  (void)self;
}

/**
 * Computes facil.io Risky256 HMAC (non-cryptographic, keyed 256-bit).
 *
 * @param key [String] The secret key
 * @param data [String] Input data to hash
 * @return [String] 32-byte binary HMAC
 */
FIO_SFUNC VALUE iodine_utils_risky256_hmac(VALUE self, VALUE key, VALUE data) {
  rb_check_type(key, RUBY_T_STRING);
  rb_check_type(data, RUBY_T_STRING);
  fio_u256 h = fio_risky256_hmac(RSTRING_PTR(key),
                                 (size_t)RSTRING_LEN(key),
                                 RSTRING_PTR(data),
                                 (size_t)RSTRING_LEN(data));
  return rb_str_new((const char *)h.u8, 32);
  (void)self;
}

/**
 * Computes facil.io Risky512 HMAC (non-cryptographic, keyed 512-bit).
 *
 * @param key [String] The secret key
 * @param data [String] Input data to hash
 * @return [String] 64-byte binary HMAC
 */
FIO_SFUNC VALUE iodine_utils_risky512_hmac(VALUE self, VALUE key, VALUE data) {
  rb_check_type(key, RUBY_T_STRING);
  rb_check_type(data, RUBY_T_STRING);
  fio_u512 h = fio_risky512_hmac(RSTRING_PTR(key),
                                 (size_t)RSTRING_LEN(key),
                                 RSTRING_PTR(data),
                                 (size_t)RSTRING_LEN(data));
  return rb_str_new((const char *)h.u8, 64);
  (void)self;
}

/**
 * Generates cryptographically secure random bytes using system CSPRNG.
 *
 * Uses arc4random_buf on BSD/macOS or /dev/urandom on Linux.
 *
 * @param bytes: [Integer] Number of bytes to generate (default: 32)
 * @return [String] Binary string of random bytes
 * @raise [RuntimeError] if CSPRNG fails
 */
FIO_SFUNC VALUE iodine_utils_secure_random(int argc, VALUE *argv, VALUE self) {
  size_t size = 32;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_SIZE_T(size, 0, "bytes", 0));
  if ((size - 1) > 0x0FFFFFFF)
    rb_raise(rb_eRangeError, "`bytes` count is out of range.");
  VALUE r = rb_str_buf_new(size);
  int result = fio_rand_bytes_secure(RSTRING_PTR(r), size);
  if (result != 0)
    rb_raise(rb_eRuntimeError, "CSPRNG failed to generate random bytes");
  rb_str_set_len(r, size);
  return r;
  (void)self;
}

FIO_SFUNC VALUE iodine_utils_blake2b(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  fio_buf_info_s key = FIO_BUF_INFO0;
  int64_t len = 64;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_BUF(key, 0, "key", 0),
                  IODINE_ARG_NUM(len, 0, "len", 0));
  if (len < 1 || len > 64)
    rb_raise(rb_eArgError, "len must be between 1 and 64");
  uint8_t out[64];
  fio_blake2b_hash(out, (size_t)len, data.buf, data.len, key.buf, key.len);
  return rb_enc_str_new((const char *)out, (long)len, IodineBinaryEncoding);
  (void)self;
}

FIO_SFUNC VALUE iodine_utils_blake2s(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  fio_buf_info_s key = FIO_BUF_INFO0;
  int64_t len = 32;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_BUF(key, 0, "key", 0),
                  IODINE_ARG_NUM(len, 0, "len", 0));
  if (len < 1 || len > 32)
    rb_raise(rb_eArgError, "len must be between 1 and 32");
  uint8_t out[32];
  fio_blake2s_hash(out, (size_t)len, data.buf, data.len, key.buf, key.len);
  return rb_enc_str_new((const char *)out, (long)len, IodineBinaryEncoding);
  (void)self;
}

FIO_SFUNC VALUE iodine_utils_hmac_sha1(VALUE self,
                                       VALUE secret,
                                       VALUE massage) {
  rb_check_type(secret, RUBY_T_STRING);
  rb_check_type(massage, RUBY_T_STRING);
  fio_buf_info_s k = IODINE_RSTR_INFO(secret);
  fio_buf_info_s m = IODINE_RSTR_INFO(massage);
  fio_sha1_s h = fio_sha1_hmac(k.buf, k.len, m.buf, m.len);
  FIO_STR_INFO_TMP_VAR(out, 40);
  fio_string_write_base64enc(&out, NULL, h.digest, 20, 0);
  return rb_str_new(out.buf, out.len);
}

FIO_SFUNC VALUE iodine_utils_hmac_poly(VALUE self,
                                       VALUE secret,
                                       VALUE massage) {
  rb_check_type(secret, RUBY_T_STRING);
  rb_check_type(massage, RUBY_T_STRING);
  fio_buf_info_s k = IODINE_RSTR_INFO(secret);
  fio_buf_info_s m = IODINE_RSTR_INFO(massage);
  fio_u256 fallback = {0};
  fio_u128 h;
  if (k.len < 256) {
    fio_memcpy255x(fallback.u8, k.buf, k.len);
    if (k.len < 10)
      fallback = fio_sha512(k.buf, k.len).u256[0];
    k.buf = (char *)fallback.u8;
  }
  fio_poly1305_auth(h.u8, m.buf, m.len, NULL, 0, k.buf);

  FIO_STR_INFO_TMP_VAR(out, 32);
  fio_string_write_base64enc(&out, NULL, h.u8, 16, 0);
  return rb_str_new(out.buf, out.len);
}

FIO_SFUNC VALUE iodine_utils_uuid(int argc, VALUE *argv, VALUE self) {
  VALUE r = Qnil;
  fio_u128 rand = iodine_random128();
  fio_buf_info_s secret = {0};
  fio_buf_info_s info = {0};
  FIO_STR_INFO_TMP_VAR(str, 128);
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(secret, 0, "secret", 0),
                  IODINE_ARG_BUF(info, 0, "info", 0));
  if (secret.buf && info.buf) {
    fio_sha512_s sh2 = fio_sha512_init();
    fio_u1024 mk = {0};
    if (secret.len <= 128) {
      fio_memcpy255x(mk.u8, secret.buf, secret.len);
      mk.u64[15] ^= secret.len;
      for (size_t i = 0; i < 16; ++i)
        mk.u64[i] ^= 0x3636363636363636ULL;
      secret.buf = (char *)mk.u8;
      secret.len = 128;
    }
    fio_sha512_consume(&sh2, secret.buf, secret.len);
    fio_sha512_consume(&sh2, info.buf, info.len);
    fio_u512 tmp = fio_sha512_finalize(&sh2);
    rand.u64[0] = tmp.u64[0] + tmp.u64[1] + tmp.u64[2] + tmp.u64[3];
    rand.u64[1] = tmp.u64[4] + tmp.u64[5] + tmp.u64[6] + tmp.u64[7];
    /* support the vendor specific UUID variant. */
    rand.u8[6] &= 0x0F;
    rand.u8[6] |= 0x80;
    rand.u8[8] &= 0x3F;
    rand.u8[8] |= 0x80;
  } else if (secret.buf || info.buf) {
    if (info.buf)
      secret = info;
    uint64_t tmp = fio_risky_hash(secret.buf, secret.len, 0);
    rand.u64[0] += tmp;
    rand.u64[1] -= tmp;
    goto random_uuid;
  } else {
  random_uuid:
    /* support the random UUID version significant bits. */
    rand.u8[6] &= 0x0F;
    rand.u8[6] |= 0x40;
    rand.u8[8] &= 0x3F;
    rand.u8[8] |= 0x80;
  }
  fio_ltoa16u(str.buf, rand.u32[0], 8);
  str.buf[8] = '-';
  fio_ltoa16u(str.buf + 9, rand.u16[2], 4);
  str.buf[13] = '-';
  fio_ltoa16u(str.buf + 14, rand.u16[3], 4);
  str.buf[18] = '-';
  fio_ltoa16u(str.buf + 19, rand.u16[4], 4);
  str.buf[23] = '-';
  fio_ltoa16u(str.buf + 24, rand.u16[5], 4);
  fio_ltoa16u(str.buf + 28, rand.u32[3], 8);
  str.buf[36] = '0';
  r = rb_str_new(str.buf, 36);
  return r;
  (void)self;
}

/** Generates random data, high entropy, not cryptographically tested. */
FIO_SFUNC VALUE iodine_utils_random(int argc, VALUE *argv, VALUE self) {
  VALUE r = Qnil;
  size_t size = 16;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_SIZE_T(size, 0, "bytes", 0));
  if ((size - 1) > 0x0FFFFFFF)
    rb_raise(rb_eRangeError, "`bytes` count is out of range.");
  r = rb_str_buf_new(size);
  iodine_random_bytes(RSTRING_PTR(r), size);
  rb_str_set_len(r, size);
  return r;
  (void)self;
}

/**
 * Generates a Time-based One-Time Password (TOTP) code.
 *
 * Returns a 6-digit TOTP code as an Integer, compatible with Google
 * Authenticator and similar apps.
 *
 *     # Generate TOTP for current time window
 *     code = Iodine::Utils.totp(secret: my_secret)
 *
 *     # Generate TOTP with custom interval (default is 30 seconds)
 *     code = Iodine::Utils.totp(secret: my_secret, interval: 60)
 *
 *     # Generate TOTP for a different time window (offset in interval units)
 *     code = Iodine::Utils.totp(secret: my_secret, offset: -1)  # previous
 * window
 *
 * Parameters:
 * - `secret:` (required) - The shared secret key (raw bytes or Base32 decoded)
 * - `offset:` (optional) - Time offset in interval units (default: 0)
 * - `interval:` (optional) - Time window in seconds (default: 30)
 *
 * Returns: Integer - A 6-digit TOTP code
 */
FIO_SFUNC VALUE iodine_utils_totp(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s secret = {0};
  int64_t offset = 0;
  size_t interval = 0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(secret, 0, "secret", 1),
                  IODINE_ARG_NUM(offset, 0, "offset", 0),
                  IODINE_ARG_SIZE_T(interval, 0, "interval", 0));
  if (!interval)
    interval = 30;

  uint32_t otp = fio_otp(secret, .offset = offset, .interval = interval);
  return UINT2NUM(otp);
  (void)self;
}

/**
 * Generates a new TOTP secret suitable for Google Authenticator.
 *
 *     # Generate a secret with default length (20 bytes)
 *     secret = Iodine::Utils.totp_secret
 *
 *     # Generate a longer secret (32 bytes)
 *     secret = Iodine::Utils.totp_secret(len: 32)
 *
 * The secret is generated using cryptographically secure random bytes
 * and encoded in Base32 (uppercase, no padding) for compatibility with
 * authenticator apps.
 *
 * Parameters:
 * - `len:` (optional) - Length of the secret in bytes (default: 20, range:
 * 10-64)
 *
 * Returns: String - Base32 encoded secret suitable for QR codes and
 * authenticator apps
 */
FIO_SFUNC VALUE iodine_utils_totp_secret(int argc, VALUE *argv, VALUE self) {
  int64_t len = 20;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_NUM(len, 0, "len", 0));

  if (len < 10 || len > 64)
    rb_raise(rb_eArgError, "len must be between 10 and 64");

  /* Generate a good enough random key */
  uint8_t key[64];
  fio_rand_bytes_secure(key, (size_t)len);

  /* Base32 encode (output is roughly 8/5 of input, plus null terminator) */
  char encoded[128];
  size_t encoded_len = fio_otp_print_key(encoded, key, (size_t)len);

  return rb_str_new(encoded, (long)encoded_len);
  (void)self;
}

/**
 * Verifies a TOTP code against a secret with time window tolerance.
 *
 *     # Verify a TOTP code with default settings
 *     valid = Iodine::Utils.totp_verify(secret: my_secret, code: user_code)
 *
 *     # Verify with larger time window (allows more clock drift)
 *     valid = Iodine::Utils.totp_verify(secret: my_secret, code: user_code,
 * window: 2)
 *
 *     # Verify with custom interval (must match the interval used to generate)
 *     valid = Iodine::Utils.totp_verify(secret: my_secret, code: user_code,
 * interval: 60)
 *
 * The window parameter specifies how many intervals to check on either side
 * of the current time. For example, window: 1 checks current ± 1 interval.
 *
 * Parameters:
 * - `secret:` (required) - The shared secret key (raw bytes or Base32 decoded)
 * - `code:` (required) - The TOTP code to verify (Integer)
 * - `window:` (optional) - Number of intervals to check on each side (default:
 * 1, range: 0-10)
 * - `interval:` (optional) - Time window in seconds (default: 30)
 *
 * Returns: true if the code is valid, false otherwise
 */
FIO_SFUNC VALUE iodine_utils_totp_verify(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s secret = FIO_BUF_INFO0;
  int64_t code = 0;
  int64_t window = 1;
  size_t interval = 0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(secret, 0, "secret", 1),
                  IODINE_ARG_NUM(code, 0, "code", 1),
                  IODINE_ARG_NUM(window, 0, "window", 0),
                  IODINE_ARG_SIZE_T(interval, 0, "interval", 0));

  if (!interval)
    interval = 30;
  if (window < 0 || window > 10)
    rb_raise(rb_eArgError, "window must be between 0 and 10");

  /* Check code against each offset in the window */
  for (int64_t offset = -window; offset <= window; ++offset) {
    uint32_t expected = fio_otp(secret, .offset = offset, .interval = interval);
    if (expected == (uint32_t)code)
      return Qtrue;
  }

  return Qfalse;
  (void)self;
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
    rb_require("rack");
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
  iodine_utils_define_methods(m);
  /* non-standard helpers */
  rb_define_singleton_method(m, "monkey_patch", iodine_utils_monkey_patch, -1);
  rb_define_singleton_method(m, "random", iodine_utils_random, -1);
  rb_define_singleton_method(m, "uuid", iodine_utils_uuid, -1);
  rb_define_singleton_method(m, "totp", iodine_utils_totp, -1);
  rb_define_singleton_method(m, "totp_secret", iodine_utils_totp_secret, -1);
  rb_define_singleton_method(m, "totp_verify", iodine_utils_totp_verify, -1);
  rb_define_singleton_method(m, "hmac512", iodine_utils_hmac512, 2);
  rb_define_singleton_method(m, "hmac256", iodine_utils_hmac256, 2);
  rb_define_singleton_method(m, "hmac160", iodine_utils_hmac_sha1, 2);
  rb_define_singleton_method(m, "hmac128", iodine_utils_hmac_poly, 2);
  rb_define_singleton_method(m, "sha256", iodine_utils_sha256, 1);
  rb_define_singleton_method(m, "sha512", iodine_utils_sha512, 1);
  rb_define_singleton_method(m, "sha3_224", iodine_utils_sha3_224, 1);
  rb_define_singleton_method(m, "sha3_256", iodine_utils_sha3_256, 1);
  rb_define_singleton_method(m, "sha3_384", iodine_utils_sha3_384, 1);
  rb_define_singleton_method(m, "sha3_512", iodine_utils_sha3_512, 1);
  rb_define_singleton_method(m, "shake128", iodine_utils_shake128, -1);
  rb_define_singleton_method(m, "shake256", iodine_utils_shake256, -1);
  rb_define_singleton_method(m, "sha1", iodine_utils_sha1, 1);
  rb_define_singleton_method(m, "crc32", iodine_utils_crc32, -1);
  rb_define_singleton_method(m, "risky_hash", iodine_utils_risky_hash, -1);
  rb_define_singleton_method(m, "risky256", iodine_utils_risky256, 1);
  rb_define_singleton_method(m, "risky512", iodine_utils_risky512, 1);
  rb_define_singleton_method(m, "risky256_hmac", iodine_utils_risky256_hmac, 2);
  rb_define_singleton_method(m, "risky512_hmac", iodine_utils_risky512_hmac, 2);
  rb_define_singleton_method(m, "secure_random", iodine_utils_secure_random, -1);
  rb_define_singleton_method(m, "blake2b", iodine_utils_blake2b, -1);
  rb_define_singleton_method(m, "blake2s", iodine_utils_blake2s, -1);



  fio_state_callback_add(FIO_CALL_IN_CHILD, iodine_random_on_fork, NULL);
}      // clang-format on
#endif /* H___IODINE_UTILS___H */
