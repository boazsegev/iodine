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

extern VALUE iodine_binary_var_id;
extern VALUE iodine_channel_var_id;
extern VALUE iodine_engine_var_id;
extern VALUE iodine_force_var_id;
extern VALUE iodine_message_var_id;
extern VALUE iodine_pattern_var_id;
extern VALUE iodine_text_var_id;

extern ID iodine_buff_var_id;
extern ID iodine_call_proc_id;
extern ID iodine_cdata_var_id;
extern ID iodine_fd_var_id;
extern ID iodine_new_func_id;
extern ID iodine_on_close_func_id;
extern ID iodine_on_data_func_id;
extern ID iodine_on_message_func_id;
extern ID iodine_on_open_func_id;
extern ID iodine_on_drained_func_id;
extern ID iodine_on_shutdown_func_id;
extern ID iodine_ping_func_id;
extern ID iodine_timeout_var_id;
extern ID iodine_to_i_func_id;
extern ID iodine_to_s_method_id;

extern rb_encoding *IodineBinaryEncoding;
extern rb_encoding *IodineUTF8Encoding;
extern int IodineBinaryEncodingIndex;
extern int IodineUTF8EncodingIndex;

UNUSED_FUNC static inline void iodine_set_fd(VALUE handler, intptr_t fd) {
  rb_ivar_set(handler, iodine_fd_var_id, LL2NUM((long)fd));
}
UNUSED_FUNC static inline intptr_t iodine_get_fd(VALUE handler) {
  return ((intptr_t)NUM2LL(rb_ivar_get(handler, iodine_fd_var_id)));
}

UNUSED_FUNC static inline void iodine_set_cdata(VALUE handler, void *protocol) {
  rb_ivar_set(handler, iodine_cdata_var_id, ULL2NUM((uintptr_t)protocol));
}
UNUSED_FUNC static inline void *iodine_get_cdata(VALUE handler) {
  return ((void *)NUM2ULL(rb_ivar_get(handler, iodine_cdata_var_id)));
}

#endif
