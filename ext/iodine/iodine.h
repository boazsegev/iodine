#ifndef H_IODINE_H
#define H_IODINE_H
/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include <ruby.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <ruby/encoding.h>
#include <ruby/io.h>
#include <ruby/thread.h>
#include <ruby/version.h>

#include "rb-call.h"
#include "rb-registry.h"

#include "facil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UNUSED_FUNC
#define UNUSED_FUNC __attribute__((unused))
#endif

extern VALUE Iodine;
extern VALUE IodineBase;
extern VALUE Iodine_Version;

extern ID fd_var_id;
extern ID timeout_var_id;
extern ID call_proc_id;
extern ID new_func_id;
extern ID on_open_func_id;
extern ID on_message_func_id;
extern ID on_data_func_id;
extern ID on_ready_func_id;
extern ID on_shutdown_func_id;
extern ID on_close_func_id;
extern ID ping_func_id;
extern ID buff_var_id;
extern ID to_s_method_id;
extern ID to_i_func;

extern rb_encoding *BinaryEncoding;
extern rb_encoding *UTF8Encoding;
extern int BinaryEncodingIndex;
extern int UTF8EncodingIndex;

UNUSED_FUNC static inline void iodine_set_fd(VALUE handler, intptr_t fd) {
  rb_ivar_set(handler, fd_var_id, LONG2NUM((long)fd));
}
UNUSED_FUNC static inline intptr_t iodine_get_fd(VALUE handler) {
  return ((intptr_t)NUM2LONG(rb_ivar_get(handler, fd_var_id)));
}

#endif
