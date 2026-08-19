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

/* *****************************************************************************
Deflate - Raw DEFLATE compression (RFC 1951)
***************************************************************************** */

/**
 * Compresses data using raw DEFLATE (no headers).
 *
 * @param data [String] Data to compress
 * @param level: [Integer] Compression level 0-9 (default: 6)
 * @return [String] Compressed data
 * @raise [RuntimeError] if compression fails
 */
FIO_SFUNC VALUE iodine_deflate_compress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t level = 6;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(level, 0, "level", 0));

  if (level < 0 || level > 9)
    rb_raise(rb_eArgError,
             "level must be between 0 and 9 (got %" PRId64 ")",
             level);

  if (!data.len)
    return rb_str_new("", 0);

  size_t out_len = fio_deflate_compress_bound(data.len);
  VALUE result = rb_str_buf_new(out_len);

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
 * @return [String] Decompressed data
 * @raise [RuntimeError] if decompression fails
 */
FIO_SFUNC VALUE iodine_deflate_decompress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc, argv, IODINE_ARG_BUF(data, 0, NULL, 1));

  if (!data.len)
    return rb_str_new("", 0);

  size_t out_len = fio_deflate_decompress_bound(data.len);
  VALUE result = rb_str_buf_new(out_len);

  size_t written =
      fio_deflate_decompress(RSTRING_PTR(result), out_len, data.buf, data.len);
  if (!written)
    rb_raise(rb_eRuntimeError,
             "Deflate decompression failed (corrupt or truncated data)");

  rb_str_set_len(result, written);
  return result;
  (void)self;
}

/* *****************************************************************************
Gzip - GZIP compression (RFC 1952)
***************************************************************************** */

/**
 * Compresses data using GZIP format.
 *
 * @param data [String] Data to compress
 * @param level: [Integer] Compression level 0-9 (default: 6)
 * @return [String] GZIP-compressed data
 * @raise [RuntimeError] if compression fails
 */
FIO_SFUNC VALUE iodine_gzip_compress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t level = 6;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(level, 0, "level", 0));

  if (level < 0 || level > 9)
    rb_raise(rb_eArgError,
             "level must be between 0 and 9 (got %" PRId64 ")",
             level);

  if (!data.len)
    return rb_str_new("", 0);

  /* gzip adds an 18-byte header/trailer around deflate data */
  size_t out_len = fio_deflate_compress_bound(data.len) + 18;
  VALUE result = rb_str_buf_new(out_len);

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
 * @return [String] Decompressed data
 * @raise [RuntimeError] if decompression fails
 */
FIO_SFUNC VALUE iodine_gzip_decompress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc, argv, IODINE_ARG_BUF(data, 0, NULL, 1));

  if (!data.len)
    return rb_str_new("", 0);

  size_t out_len = fio_deflate_decompress_bound(data.len);
  VALUE result = rb_str_buf_new(out_len);

  size_t written =
      fio_gzip_decompress(RSTRING_PTR(result), out_len, data.buf, data.len);
  if (!written)
    rb_raise(rb_eRuntimeError,
             "Gzip decompression failed (corrupt or truncated data)");

  rb_str_set_len(result, written);
  return result;
  (void)self;
}

/* *****************************************************************************
Brotli - Brotli compression (RFC 7932)
***************************************************************************** */

/**
 * Compresses data using Brotli.
 *
 * @param data [String] Data to compress
 * @param quality: [Integer] Compression quality 1-4 (default: 4)
 * @return [String] Brotli-compressed data
 * @raise [RuntimeError] if compression fails
 */
FIO_SFUNC VALUE iodine_brotli_compress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  int64_t quality = 4;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_NUM(quality, 0, "quality", 0));

  if (quality < 1 || quality > 4)
    rb_raise(rb_eArgError,
             "quality must be between 1 and 4 (got %" PRId64 ")",
             quality);

  if (!data.len)
    return rb_str_new("", 0);

  size_t out_len = fio_brotli_compress_bound(data.len);
  VALUE result = rb_str_buf_new(out_len);

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
 * @return [String] Decompressed data
 * @raise [RuntimeError] if decompression fails
 */
FIO_SFUNC VALUE iodine_brotli_decompress(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc, argv, IODINE_ARG_BUF(data, 0, NULL, 1));

  if (!data.len)
    return rb_str_new("", 0);

  size_t out_len = fio_brotli_decompress_bound(data.len);
  VALUE result = rb_str_buf_new(out_len);

  size_t written =
      fio_brotli_decompress(RSTRING_PTR(result), out_len, data.buf, data.len);
  if (!written)
    rb_raise(rb_eRuntimeError,
             "Brotli decompression failed (corrupt or truncated data)");

  rb_str_set_len(result, written);
  return result;
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
