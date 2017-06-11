/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "rb-call.h"
#include <pthread.h>
#include <ruby.h>
#include <ruby/thread.h>

#if __STDC_VERSION__ < 201112L || __STDC_NO_THREADS__
#define _Thread_local __thread
#endif

///////////////
// this is a simple helper that calls Ruby methods on Ruby objects while within
// a non-GVL ruby thread zone.
struct RubyArgCall {
  VALUE obj;
  int argc;
  VALUE *argv;
  VALUE returned;
  ID method;
};

// running the actual method call
static VALUE run_ruby_method_unsafe(VALUE tsk_) {
  struct RubyArgCall *task = (void *)tsk_;
  return rb_funcall2(task->obj, task->method, task->argc, task->argv);
}

////////////////////////////////////////////////////////////////////////////
// Handling exceptions (printing the backtrace doesn't really work well).
static void *handle_exception(void *ignr) {
  (void)ignr;
  VALUE exc = rb_errinfo();
  if (exc != Qnil) {
    VALUE msg = RubyCaller.call(exc, rb_intern("message"));
    VALUE exc_class = rb_class_name(CLASS_OF(exc));
    VALUE bt = RubyCaller.call(exc, rb_intern("backtrace"));
    if (TYPE(bt) == T_ARRAY) {
      bt = rb_ary_join(bt, rb_str_new_literal("\n"));
      fprintf(stderr, "Iodine caught an unprotected exception - %.*s: %.*s\n%s",
              (int)RSTRING_LEN(exc_class), RSTRING_PTR(exc_class),
              (int)RSTRING_LEN(msg), RSTRING_PTR(msg), StringValueCStr(bt));
    } else {
      fprintf(stderr,
              "Iodine caught an unprotected exception - %.*s: %.*s\n"
              "No backtrace available.\n",
              (int)RSTRING_LEN(exc_class), RSTRING_PTR(exc_class),
              (int)RSTRING_LEN(msg), RSTRING_PTR(msg));
    }
    rb_backtrace();
    rb_set_errinfo(Qnil);
  }
  return (void *)Qnil;
}

// GVL gateway
static void *run_ruby_method_within_gvl(void *tsk_) {
  struct RubyArgCall *task = tsk_;
  int state = 0;
  task->returned = rb_protect(run_ruby_method_unsafe, (VALUE)(task), &state);
  if (state)
    handle_exception(NULL);
  return task;
}

////////////////////////////////////////////////////////////////////////////
// GVL state.

// a thread specific global variable that lets us know if we're in the GVL
static _Thread_local char in_gvl = 0;
static char check_in_gvl(void) { return in_gvl; }

////////////////////////////////////////////////////////////////////////////
// Calling C functions.
static void *call_c(void *(*func)(void *), void *arg) {
  if (in_gvl) {
    return func(arg);
  }
  void *ret;
  in_gvl = 1;
  ret = rb_thread_call_with_gvl(func, arg);
  in_gvl = 0;
  return ret;
}

////////////////////////////////////////////////////////////////////////////
// A simple (and a bit lighter) design for when there's no need for arguments.

// wrapping any API calls for exception management AND GVL entry
static VALUE call(VALUE obj, ID method) {
  struct RubyArgCall task = {.obj = obj, .method = method};
  call_c(run_ruby_method_within_gvl, &task);
  return task.returned;
}

////////////////////////////////////////////////////////////////////////////
// A heavier (memory) design for when we're passing arguments around.

// wrapping any API calls for exception management AND GVL entry
static VALUE call_arg(VALUE obj, ID method, int argc, VALUE *argv) {
  struct RubyArgCall task = {
      .obj = obj, .method = method, .argc = argc, .argv = argv};
  call_c(run_ruby_method_within_gvl, &task);
  return task.returned;
}

////////////////////////////////////////////////////////////////////////////
// the API interface
struct _Ruby_Method_Caller_Class_ RubyCaller = {
    .call = call, .call2 = call_arg, .call_c = call_c, .in_gvl = check_in_gvl,
};
