/*
  copyright: Boaz Segev, 2017-2019
  license: MIT

  Feel free to copy, use and enjoy according to the license specified.
*/
#ifndef H_WEBSOCKET_DEFLATE_H
/**\file

   A single file WebSocket permessage-deflate wrapper

*/
#define H_WEBSOCKET_DEFLATE_H
#include <fiobj.h>
#include <stdlib.h>
#include <zlib.h>

#define WS_CHUNK 16384

z_stream *new_z_stream() {
  z_stream *strm = malloc(sizeof(*strm));

  *strm = (z_stream){
    .zalloc = Z_NULL,
    .zfree = Z_NULL,
    .opaque = Z_NULL,
  };

  return strm;
}

z_stream *new_deflator() {
  z_stream *strm = new_z_stream();

  int ret = deflateInit2(strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                         -MAX_WBITS, 4, Z_DEFAULT_STRATEGY);

  return strm;
}

z_stream *new_inflator() {
  z_stream *strm = new_z_stream();

  inflateInit2(strm, -MAX_WBITS);

  return strm;
}

int deflate_message(fio_str_info_s src, FIOBJ dest, z_stream *strm) {
  int ret, flush;
  unsigned have;
  unsigned char out[WS_CHUNK];

  strm->avail_in = src.len;
  strm->next_in = src.data;

  do {
    strm->avail_out = WS_CHUNK;
    strm->next_out = out;
    ret = deflate(strm, Z_SYNC_FLUSH);
    have = WS_CHUNK - strm->avail_out;
    fiobj_str_write(dest, out, have);
  } while (strm->avail_out == 0);

  return Z_OK;
}

int inflate_message(fio_str_info_s src, FIOBJ dest, z_stream *strm) {
  int ret;
  unsigned have;
  unsigned char out[WS_CHUNK];

  strm->avail_in = src.len;
  strm->next_in = src.data;

  do {
    strm->avail_out = WS_CHUNK;
    strm->next_out = out;
    ret = inflate(strm, Z_SYNC_FLUSH);
    switch (ret) {
    case Z_NEED_DICT:
      ret = Z_DATA_ERROR;
    case Z_DATA_ERROR:
    case Z_MEM_ERROR:
      (void)inflateEnd(strm);
      return ret;
    }
    have = WS_CHUNK - strm->avail_out;
    fiobj_str_write(dest, out, have);
  } while (strm->avail_out == 0);

  return ret;
}
#endif
