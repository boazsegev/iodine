/*
copyright: Boaz segev, 2016
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "rb-registry.h"
#include "rb-call.h"
#include "lib-server.h"
#include <ruby.h>
#include <ruby/thread.h>
#include <ruby/encoding.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
Every object (the Iodine core, the protocols) will have a reference to their
respective `struct Server` object, allowing them to invoke server methods
(i.e. `write`, `close`, etc')...

This data (a C pointer) needs to be attached to the Ruby objects via a T_DATA
object variable. These T_DATA types define memory considerations for the GC.

We need to make sure Ruby doesn't free our server along with our object...
*/
extern struct rb_data_type_struct iodine_core_server_type;
/** a macro helper function to embed a server pointer in an object */
#define set_server(object, srv)        \
  rb_ivar_set((object), server_var_id, \
              TypedData_Wrap_Struct(rServer, &iodine_core_server_type, (srv)))

/** a macro helper to get the server pointer embeded in an object */
#define get_server(object) \
  (server_pt) DATA_PTR(rb_ivar_get((object), server_var_id))

extern VALUE rIodine;
extern VALUE rServer;
extern VALUE rBase;
extern VALUE rDynProtocol;
extern int BinaryEncodingIndex;     /* encoding index */
extern rb_encoding* BinaryEncoding; /* encoding object */

/** the Iodine dynamic protocol */
extern struct Protocol DynamicProtocol;

/** the Idle implementation... assumes that settings.udata == (void*)server */
void on_idle_server_callback(struct Server* srv);

void iodine_add_helper_methods(VALUE klass);
