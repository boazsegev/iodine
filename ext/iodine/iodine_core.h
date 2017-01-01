/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef IODINE_CORE_H
#define IODINE_CORE_H
#include "libserver.h"
#include "rb-call.h"
#include "rb-registry.h"
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/thread.h>
#include <ruby/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __unused
#define __unused __attribute__((unused))
#endif

extern rb_encoding *BinaryEncoding;
extern rb_encoding *UTF8Encoding;
extern int BinaryEncodingIndex;
extern int UTF8EncodingIndex;

extern VALUE Iodine;
extern VALUE IodineBase;
extern VALUE Iodine_Version;
extern ID call_proc_id;
extern ID on_start_func_id;
extern ID on_finish_func_id;
extern ID new_func_id;
extern ID on_open_func_id;
extern ID on_message_func_id;
extern ID on_data_func_id;
extern ID on_ready_func_id;
extern ID on_shutdown_func_id;
extern ID on_close_func_id;
extern ID ping_func_id;
extern ID buff_var_id;
extern ID fd_var_id;
extern ID timeout_var_id;
extern ID to_s_method_id;

__unused static inline void iodine_set_fd(VALUE handler, intptr_t fd) {
  rb_ivar_set(handler, fd_var_id, LONG2NUM((long)fd));
}
__unused static inline intptr_t iodine_get_fd(VALUE handler) {
  return ((intptr_t)NUM2LONG(rb_ivar_get(handler, fd_var_id)));
}
/* *****************************************************************************
The Core dynamic Iodine protocol - all protocols using `each` should follow
this design.
*/
typedef struct {
  protocol_s protocol;
  VALUE handler;
} dyn_protocol_s;

#define dyn_prot(protocol) ((dyn_protocol_s *)(protocol))

/* "upgrades" a connection to a dynamic generic protocol */
VALUE iodine_upgrade2basic(intptr_t fduuid, VALUE handler);
/* runs a task for each connection in the service. */
void iodine_run_each(intptr_t origin, const char *service, VALUE block);

#endif
