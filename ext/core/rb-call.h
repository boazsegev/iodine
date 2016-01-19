#include <ruby.h>

///////////////
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
} RubyCaller;
