#include <ruby.h>

////////////////////////////////////////////////////////////////////////
// The Ruby framework manages it's own contextc switching and memory...
// this means that when we make calls to Ruby (i.e. creating Ruby objects),
// we rick currupting the Ruby framework. Also, Ruby uses all these signals (for
// it's context switchings) and long-jumps (i.e. Ruby exceptions) that can drive
// the C code a little nuts...
//
// Seperation is reqiured :-)
//
// This is a simple helper that calls Ruby methods on Ruby objects while within
// a non-GVL ruby thread zone.
// Simply use:
//
//     RubyCaller.call(object, method_id);
//
// If you're within a the GVL, RubyCaller provides automated exception handling.
// use:
//
//     RubyCaller.call_unsafe(object, method_id);
//
// To pass arguments to the ruby object's method, use the `call2` and
// `call_unsafe2`
// methods instead. i.e.:
//
//     RubyCaller.call(object, method_id, argc, argv);
//
extern struct _Ruby_Method_Caller_Class_ {
  VALUE (*call)(VALUE object, ID method_id);
  VALUE (*call_unsafe)(VALUE object, ID method_id);
  VALUE (*call2)(VALUE obj, ID method, int argc, VALUE* argv);
  VALUE (*call_unsafe2)(VALUE obj, ID method, int argc, VALUE* argv);
} RubyCaller;
