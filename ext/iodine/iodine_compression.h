#ifndef H___IODINE_COMPRESSION___H
#define H___IODINE_COMPRESSION___H
#include "iodine.h"

/* *****************************************************************************
Iodine::Base::Compression - Data Compression

Provides access to compression/decompression algorithms:
- Deflate: Raw DEFLATE (RFC 1951) - no headers
- Gzip: GZIP format (RFC 1952) - deflate with gzip wrapper
- Brotli: Brotli compression (RFC 7932)
***************************************************************************** */

static VALUE iodine_rb_COMPRESSION;
static VALUE iodine_rb_DEFLATE;
static VALUE iodine_rb_GZIP;
static VALUE iodine_rb_BROTLI;

/* Bound decompression of untrusted streams before allocating output. Callers
 * processing larger trusted data may raise this limit explicitly. */
#define IODINE_COMPRESSION_MAX_OUTPUT (64 * 1024 * 1024)

typedef size_t (*iodine_decompress_fn)(void *, size_t, const void *, size_t);

FIO_SFUNC VALUE iodine_compression_decompress(fio_buf_info_s data,
                                               size_t max_output,
                                               iodine_decompress_fn decode,
                                               const char *name) {
  if (!data.len)
    rb_raise(rb_eRuntimeError, "%s decompression failed (empty input)", name);
  size_t required = decode(NULL, 0, data.buf, data.len);
  if (!required) {
    /* Upstream returns 0 both for invalid input and valid empty output.
     * Recognize the canonical empty streams our encoders emit. */
    static const uint8_t raw_empty[] = {1, 0, 0, 255, 255};
    static const uint8_t gzip_empty[] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255,
                                         1, 0, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t brotli_empty[] = {6};
    if ((decode == fio_deflate_decompress && data.len == sizeof(raw_empty) &&
         !memcmp(data.buf, raw_empty, sizeof(raw_empty))) ||
        (decode == fio_gzip_decompress && data.len == sizeof(gzip_empty) &&
         !memcmp(data.buf, gzip_empty, sizeof(gzip_empty))) ||
        (decode == fio_brotli_decompress && data.len == sizeof(brotli_empty) &&
         !memcmp(data.buf, brotli_empty, sizeof(brotli_empty))))
      return rb_str_new("", 0);
    rb_raise(rb_eRuntimeError, "%s decompression failed (invalid data)", name);
  }
  if (required > max_output)
    rb_raise(rb_eArgError, "%s output exceeds max_output", name);
  if (required > LONG_MAX)
    rb_raise(rb_eArgError, "%s output exceeds Ruby string limit", name);
  VALUE result = rb_str_buf_new((long)required);
  size_t written = decode(RSTRING_PTR(result), required, data.buf, data.len);
  if (!written || written > required)
    rb_raise(rb_eRuntimeError, "%s decompression failed (invalid data)", name);
  rb_str_set_len(result, (long)written);
  return result;
}

FIO_SFUNC long iodine_compression_bound(size_t input, size_t bound) {
  /* Ruby strings and rb_str_buf_new use long lengths. */
  if (bound < input || bound > LONG_MAX)
    rb_raise(rb_eArgError, "compressed output exceeds Ruby string limit");
  return (long)bound;
}

/* *****************************************************************************
Deflate - Raw DEFLATE compression (RFC 1951)
***************************************************************************** */

/**
 * Compresses data using raw DEFLATE (no headers).
 *
 * @param data [String] Data to compress
 * @param level: [Integer] Compression level 0-9 (default: 3)
 * @return [String] Compressed data
 * @raise [RuntimeError] if compression fails
 */
FIO_SFUNC VALUE iodine_deflate_compress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t level = 3;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(level, 0, "level", 0));

  if (level < 0 || level > 9)
    rb_raise(rb_eArgError,
             "level must be between 0 and 9 (got %" PRId64 ")",
             level);

  /* Check before calling the upstream bound (which uses size_t arithmetic). */
  if (data.len > LONG_MAX / 2)
    rb_raise(rb_eArgError, "compressed output exceeds Ruby string limit");
  size_t out_len = fio_deflate_compress_bound(data.len);
  VALUE result = rb_str_buf_new(iodine_compression_bound(data.len, out_len));

  size_t written = fio_deflate_compress(RSTRING_PTR(result),
                                        out_len,
                                        data.buf,
                                        data.len,
                                        (int)level);
  if (!written)
    rb_raise(rb_eRuntimeError, "Deflate compression failed");

  rb_str_set_len(result, written);
  return result;
  (void)self;
}

/**
 * Decompresses raw DEFLATE data.
 *
 * @param data [String] Compressed data (raw DEFLATE, no headers)
 * @param max_output: [Integer] Maximum output bytes (default: 64 MiB)
 * @return [String] Decompressed data
 * @raise [RuntimeError] if decompression fails
 */
FIO_SFUNC VALUE iodine_deflate_decompress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  size_t max_output = IODINE_COMPRESSION_MAX_OUTPUT;

  iodine_rb2c_arg(argc, argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_SIZE_T(max_output, 0, "max_output", 0));

  return iodine_compression_decompress(data, max_output,
                                       fio_deflate_decompress, "Deflate");
  (void)self;
}

/* *****************************************************************************
Gzip - GZIP compression (RFC 1952)
***************************************************************************** */

/**
 * Compresses data using GZIP format.
 *
 * @param data [String] Data to compress
 * @param level: [Integer] Compression level 0-9 (default: 3)
 * @return [String] GZIP-compressed data
 * @raise [RuntimeError] if compression fails
 */
FIO_SFUNC VALUE iodine_gzip_compress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t level = 3;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(level, 0, "level", 0));

  if (level < 0 || level > 9)
    rb_raise(rb_eArgError,
             "level must be between 0 and 9 (got %" PRId64 ")",
             level);

  /* gzip adds an 18-byte header/trailer around deflate data */
  /* Check before calling the upstream bound (which uses size_t arithmetic). */
  if (data.len > LONG_MAX / 2)
    rb_raise(rb_eArgError, "compressed output exceeds Ruby string limit");
  size_t out_len = fio_deflate_compress_bound(data.len) + 18;
  VALUE result = rb_str_buf_new(iodine_compression_bound(data.len, out_len));

  size_t written = fio_gzip_compress(RSTRING_PTR(result),
                                     out_len,
                                     data.buf,
                                     data.len,
                                     (int)level);
  if (!written)
    rb_raise(rb_eRuntimeError, "Gzip compression failed");

  rb_str_set_len(result, written);
  return result;
  (void)self;
}

/**
 * Decompresses GZIP data.
 *
 * @param data [String] GZIP-compressed data
 * @param max_output: [Integer] Maximum output bytes (default: 64 MiB)
 * @return [String] Decompressed data
 * @raise [RuntimeError] if decompression fails
 */
FIO_SFUNC VALUE iodine_gzip_decompress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  size_t max_output = IODINE_COMPRESSION_MAX_OUTPUT;

  iodine_rb2c_arg(argc, argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_SIZE_T(max_output, 0, "max_output", 0));

  return iodine_compression_decompress(data, max_output,
                                       fio_gzip_decompress, "Gzip");
  (void)self;
}

/* *****************************************************************************
Brotli - Brotli compression (RFC 7932)
***************************************************************************** */

/**
 * Compresses data using Brotli.
 *
 * @param data [String] Data to compress
 * @param quality: [Integer] Compression quality 1-6 (default: 3)
 * @return [String] Brotli-compressed data
 * @raise [RuntimeError] if compression fails
 */
FIO_SFUNC VALUE iodine_brotli_compress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t quality = 3;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(quality, 0, "quality", 0));

  if (quality < 1 || quality > 6)
    rb_raise(rb_eArgError,
             "quality must be between 1 and 6 (got %" PRId64 ")",
             quality);

  if (data.len > LONG_MAX - (data.len >> 2) - 1024)
    rb_raise(rb_eArgError, "compressed output exceeds Ruby string limit");
  size_t out_len = fio_brotli_compress_bound(data.len);
  VALUE result = rb_str_buf_new(iodine_compression_bound(data.len, out_len));

  size_t written = fio_brotli_compress(RSTRING_PTR(result),
                                       out_len,
                                       data.buf,
                                       data.len,
                                       (int)quality);
  if (!written)
    rb_raise(rb_eRuntimeError, "Brotli compression failed");

  rb_str_set_len(result, written);
  return result;
  (void)self;
}

/**
 * Decompresses Brotli data.
 *
 * @param data [String] Brotli-compressed data
 * @param max_output: [Integer] Maximum output bytes (default: 64 MiB)
 * @return [String] Decompressed data
 * @raise [RuntimeError] if decompression fails
 */
FIO_SFUNC VALUE iodine_brotli_decompress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  size_t max_output = IODINE_COMPRESSION_MAX_OUTPUT;

  iodine_rb2c_arg(argc, argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_SIZE_T(max_output, 0, "max_output", 0));

  return iodine_compression_decompress(data, max_output,
                                       fio_brotli_decompress, "Brotli");
  (void)self;
}

/* *****************************************************************************
Module Initialization
***************************************************************************** */

static void Init_Iodine_Compression(void) {
  /* Iodine::Base::Compression */
  iodine_rb_COMPRESSION =
      rb_define_module_under(iodine_rb_IODINE_BASE, "Compression");
  STORE.hold(iodine_rb_COMPRESSION);

  /* Iodine::Base::Compression::Deflate */
  iodine_rb_DEFLATE = rb_define_module_under(iodine_rb_COMPRESSION, "Deflate");
  STORE.hold(iodine_rb_DEFLATE);
  rb_define_module_function(iodine_rb_DEFLATE,
                            "compress",
                            iodine_deflate_compress,
                            -1);
  rb_define_module_function(iodine_rb_DEFLATE,
                            "decompress",
                            iodine_deflate_decompress,
                            -1);

  /* Iodine::Base::Compression::Gzip */
  iodine_rb_GZIP = rb_define_module_under(iodine_rb_COMPRESSION, "Gzip");
  STORE.hold(iodine_rb_GZIP);
  rb_define_module_function(iodine_rb_GZIP,
                            "compress",
                            iodine_gzip_compress,
                            -1);
  rb_define_module_function(iodine_rb_GZIP,
                            "decompress",
                            iodine_gzip_decompress,
                            -1);

  /* Iodine::Base::Compression::Brotli */
  iodine_rb_BROTLI = rb_define_module_under(iodine_rb_COMPRESSION, "Brotli");
  STORE.hold(iodine_rb_BROTLI);
  rb_define_module_function(iodine_rb_BROTLI,
                            "compress",
                            iodine_brotli_compress,
                            -1);
  rb_define_module_function(iodine_rb_BROTLI,
                            "decompress",
                            iodine_brotli_decompress,
                            -1);
}

#endif /* H___IODINE_COMPRESSION___H */
