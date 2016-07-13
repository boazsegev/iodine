/*
copyright: Boaz segev, 2016
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef IODINE_CORE_H
#define IODINE_CORE_H
#include "rb-registry.h"
#include "rb-call.h"
#include "libserver.h"
#include <ruby.h>
#include <ruby/thread.h>
#include <ruby/encoding.h>
#include <ruby/version.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __unused
#define __unused __attribute__((unused))
#endif

extern rb_encoding* BinaryEncoding;
extern rb_encoding* UTF8Encoding;
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

#define dyn_prot(protocol) ((dyn_protocol_s*)(protocol))

/* "upgrades" a connection to a dynamic generic protocol */
VALUE iodine_upgrade2basic(intptr_t fduuid, VALUE handler);
/* runs a task for each connection in the service. */
void iodine_run_each(intptr_t origin, const char* service, VALUE block);
//
// /**
// Every object (the Iodine core, the protocols) will have a reference to their
// respective `struct Server` object, allowing them to invoke server methods
// (i.e. `write`, `close`, etc')...
//
// This data (a C pointer) needs to be attached to the Ruby objects via a T_DATA
// object variable. These T_DATA types define memory considerations for the GC.
//
// We need to make sure Ruby doesn't free our server along with our object...
// */
// extern struct rb_data_type_struct iodine_core_server_type;
// /** a macro helper function to embed a server pointer in an object */
// #define set_server(object, srv) \
//   rb_ivar_set(                  \
//       (object), server_var_id,  \
//       TypedData_Wrap_Struct(rServer, &iodine_core_server_type, (void*)(srv)))
//
// /** a macro helper to get the server pointer embeded in an object */
// #define get_server(object) \
//   (server_pt) DATA_PTR(rb_ivar_get((object), server_var_id))
//
// extern VALUE rIodine;
// extern VALUE rServer;
// extern VALUE rBase;
// extern VALUE rDynProtocol;
// extern int BinaryEncodingIndex;     /* encoding index */
// extern rb_encoding* BinaryEncoding; /* encoding object */
//
// /** the Iodine dynamic protocol */
// extern struct Protocol DynamicProtocol;
//
// /** the Idle implementation... assumes that settings.udata == (void*)server
// */
// void on_idle_server_callback();
//
// void iodine_add_helper_methods(VALUE klass);
#endif
