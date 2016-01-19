#include <ruby.h>

////////////////////////////////////////////////////////////////////////
// The Ruby framework manages it's own contextc switching and memory...
// this means that when we make calls to Ruby (i.e. creating Ruby objects),
// we rick currupting the Ruby framework. Also, Ruby uses all these signals (for
// it's context switchings) and long-jumps (i.e. Ruby exceptions) that can drive
// the C code a little nuts...
//
// seperation is reqiured :-)
//
// this is a simple helper that calls Ruby methods on Ruby objects while within
// a non-GVL ruby thread zone.
// Simply use:
//
//     RubyCaller.call(VALUE object, ID method)
//
// todo: add a method for calling ruby code with arguments... I'll need it.
//
extern struct _Ruby_Method_Caller_Class_ {
  VALUE (*call)(VALUE object, ID method_id);
  VALUE (*call_unsafe)(VALUE object, ID method_id);
} RubyCaller;
