#ifndef RB_CALL_H
#define RB_CALL_H
#include <ruby.h>

#define RB_CALL_VERSION "0.2.0"

////////////////////////////////////////////////////////////////////////
// The Ruby framework manages it's own contextc switching and memory...
// this means that when we make calls to Ruby (i.e. creating Ruby objects),
// we risk currupting the Ruby framework. Also, Ruby uses all these signals (for
// it's context switchings) and long-jumps (i.e. Ruby exceptions) that can drive
// the C code a little nuts...
//
// Seperation is reqiured :-)
//
// This is a simple helper that calls Ruby methods on Ruby objects and C
// functions that require acess to the Ruby API, while managing the GVL lock
// status as required.
//
// To call a Ruby object's method, simply use:
//
//     RubyCaller.call(object, method_id);
//
// To pass arguments to the ruby object's method, use the `call2` method. i.e.:
//
//     RubyCaller.call2(object, method_id, argc, argv);
//
//
// This library keeps track of the thread, adjusting the method to be called in
// case the thread is already within the GVL.
//
// The library assums that it is within a Ruby thread that is was released of
// the GVL using `rb_thread_call_without_gvl2` or `rb_thread_call_without_gvl`.
//
extern struct _Ruby_Method_Caller_Class_ {
  /** calls a Object's ruby method, adjusting for the GVL if needed */
  VALUE (*call)(VALUE object, ID method_id);
  /**
calls a Object's ruby method with arguments, adjusting for the GVL if needed
  */
  VALUE (*call2)(VALUE obj, ID method, int argc, VALUE* argv);
  /**
calls a C method that requires the GVL for access to the Ruby API, managing the
GVL state as required.
  */
  void* (*call_c)(void* (*func)(void*), void* arg);
  /**
returns the thread's GVL status (1 if inside a GVL and 0 if free from the
GVL).
  */
  char (*in_gvl)(void);
} RubyCaller;

#endif  // RB_CALL_H
