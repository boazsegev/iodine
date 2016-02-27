#include "rb-call.h"
#include <ruby.h>
#include <ruby/thread.h>
#include <pthread.h>

///////////////
// this is a simple helper that calls Ruby methods on Ruby objects while within
// a non-GVL ruby thread zone.

// a structure for Ruby API calls
struct RubySimpleCall {
  VALUE obj;
  VALUE returned;
  ID method;
};
struct RubyArgCall {
  VALUE obj;
  int argc;
  VALUE* argv;
  VALUE returned;
  ID method;
};

// a thread specific global variable that lets us know if we're in the GVL
_Thread_local static char in_gvl = 0;
static char check_in_gvl(void) {
  return in_gvl;
}

////////////////////////////////////////////////////////////////////////////
// Calling C functions.
static void* call_c(void* (*func)(void*), void* arg) {
  if (in_gvl) {
    return func(arg);
  }
  void* ret;
  in_gvl = 1;
  ret = rb_thread_call_with_gvl(func, arg);
  in_gvl = 0;
  return ret;
}

static void* handle_exception(void* _) {
  VALUE exc = rb_errinfo();
  if (exc != Qnil) {
    VALUE msg = rb_attr_get(exc, rb_intern("mesg"));
    VALUE exc_class = rb_class_name(CLASS_OF(exc));
    fprintf(stderr, "%.*s: %.*s\n", (int)RSTRING_LEN(exc_class),
            RSTRING_PTR(exc_class), (int)RSTRING_LEN(msg), RSTRING_PTR(msg));
    rb_backtrace();
    rb_set_errinfo(Qnil);
  }
  return (void*)exc;
}

////////////////////////////////////////////////////////////////////////////
// A simple (and a bit lighter) design for when there's no need for arguments.

// running the actual method call
static VALUE run_ruby_method_unsafe(VALUE _tsk) {
  struct RubySimpleCall* task = (void*)_tsk;
  return rb_funcall2(task->obj, task->method, 0, NULL);
}

// GVL gateway
static void* run_ruby_method_within_gvl(void* _tsk) {
  struct RubySimpleCall* task = _tsk;
  int state = 0;
  task->returned = rb_protect(run_ruby_method_unsafe, (VALUE)(task), &state);
  if (state)
    handle_exception(NULL);
  return task;
}

// wrapping any API calls for exception management AND GVL entry
static VALUE call(VALUE obj, ID method) {
  struct RubySimpleCall task = {.obj = obj, .method = method};
  call_c(run_ruby_method_within_gvl, &task);
  return task.returned;
}

////////////////////////////////////////////////////////////////////////////
// A heavier (memory) design for when we're passing arguments around.

// running the actual method call
static VALUE run_argv_method_unsafe(VALUE _tsk) {
  struct RubyArgCall* task = (void*)_tsk;
  return rb_funcall2(task->obj, task->method, task->argc, task->argv);
}

// GVL gateway
static void* run_argv_method_within_gvl(void* _tsk) {
  struct RubyArgCall* task = _tsk;
  int state = 0;
  task->returned = rb_protect(run_argv_method_unsafe, (VALUE)(task), &state);
  if (state)
    handle_exception(NULL);
  return task;
}

// wrapping any API calls for exception management AND GVL entry
static VALUE call_arg(VALUE obj, ID method, int argc, VALUE* argv) {
  struct RubyArgCall task = {
      .obj = obj, .method = method, .argc = argc, .argv = argv};
  call_c(run_argv_method_within_gvl, &task);
  return task.returned;
}

////////////////////////////////////////////////////////////////////////////
// the API interface
struct _Ruby_Method_Caller_Class_ RubyCaller = {
    .call = call,
    .call2 = call_arg,
    .call_c = call_c,
    .in_gvl = check_in_gvl,
};
