#include "rb-call.h"
#include <ruby.h>
#include <ruby/thread.h>

///////////////
// this is a simple helper that calls Ruby methods on Ruby objects while within
// a non-GVL ruby thread zone.

// a structure for Ruby API calls
struct RubyApiCall {
  VALUE obj;
  VALUE returned;
  ID method;
};

// running the actual method call
static VALUE run_ruby_method_unsafe(VALUE _tsk) {
  struct RubyApiCall* task = (void*)_tsk;
  return rb_funcall(task->obj, task->method, 0);
}
// GVL gateway
static void* run_ruby_method_within_gvl(void* _tsk) {
  struct RubyApiCall* task = _tsk;
  int state = 0;
  task->returned = rb_protect(run_ruby_method_unsafe, (VALUE)(task), &state);
  if (state) {
    VALUE exc = rb_errinfo();
    if (exc != Qnil) {
      VALUE msg = rb_attr_get(exc, rb_intern("mesg"));
      VALUE exc_class = rb_class_name(CLASS_OF(exc));
      fprintf(stderr, "%.*s: %.*s\n", (int)RSTRING_LEN(exc_class),
              RSTRING_PTR(exc_class), (int)RSTRING_LEN(msg), RSTRING_PTR(msg));
      rb_backtrace();
      rb_set_errinfo(Qnil);
    }
  }
  return task;
}
// wrapping any API calls for exception management
static VALUE call(VALUE obj, ID method) {
  struct RubyApiCall task = {.obj = obj, .method = method};
  rb_thread_call_with_gvl(run_ruby_method_within_gvl, &task);
  return task.returned;
}

// the API interface
struct _Ruby_Method_Caller_Class_ RubyCaller = {
    .call = call,
};
