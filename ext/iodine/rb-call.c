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

typedef enum {
  RUBY_TASK,
  C_TASK,
} iodine_task_type_en;

typedef struct {
  iodine_task_type_en type;
  VALUE obj;
  int argc;
  VALUE *argv;
  VALUE returned;
  ID method;
  int exception;
} iodine_rb_task_s;

typedef struct {
  iodine_task_type_en type;
  void *(*func)(void *);
  void *arg;
} iodine_c_task_s;

// running the actual method call
static VALUE iodine_ruby_caller_perform(VALUE tsk_) {
  switch (*(iodine_task_type_en *)tsk_) {
  case RUBY_TASK: {
    iodine_rb_task_s *task = (void *)tsk_;
    return rb_funcall2(task->obj, task->method, task->argc, task->argv);
  }
  case C_TASK: {
    iodine_c_task_s *task = (void *)tsk_;
    return (VALUE)task->func(task->arg);
  }
  }
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
    fprintf(stderr, "\n\n");
    rb_set_errinfo(Qnil);
  }
  return (void *)Qnil;
}

/* wrap the function call in the exception handling code */
static void *iodine_protected_call(void *tsk_) {
  int state = 0;
  VALUE ret = rb_protect(iodine_ruby_caller_perform, (VALUE)(tsk_), &state);
  if (state) {
    handle_exception(NULL);
  }
  return (void *)ret;
}

////////////////////////////////////////////////////////////////////////////
// GVL state.

// a thread specific global variable that lets us know if we're in the GVL
static _Thread_local char in_gvl = 0;
static char iodine_rb_check_in_gvl(void) { return in_gvl; }

static void iodine_rb_set_gvl_state(char state) { in_gvl = state; }

////////////////////////////////////////////////////////////////////////////
// Calling C functions.
static inline void *iodine_rb_call_c(void *(*func)(void *), void *arg) {
  if (in_gvl) {
    return func(arg);
  }
  iodine_c_task_s task = {.type = C_TASK, .func = func, .arg = arg};
  void *ret;
  in_gvl = 1;
  ret = rb_thread_call_with_gvl(iodine_protected_call, &task);
  in_gvl = 0;
  return ret;
}

static void *iodine_rb_leave_gvl(void *(*func)(void *), void *arg) {
  if (!in_gvl) {
    return func(arg);
  }
  in_gvl = 0;
  void *ret = rb_thread_call_without_gvl(func, arg, NULL, NULL);
  in_gvl = 1;
  return ret;
}

////////////////////////////////////////////////////////////////////////////
// A heavier (memory) design for when we're passing arguments around.

// wrapping any API calls for exception management AND GVL entry
static VALUE iodin_rb_call_arg(VALUE obj, ID method, int argc, VALUE *argv) {
  iodine_rb_task_s task = {.type = RUBY_TASK,
                           .obj = obj,
                           .method = method,
                           .argc = argc,
                           .argv = argv};
  void *ret;
  if (in_gvl)
    return (VALUE)rb_funcall2(obj, method, argc, argv);
  in_gvl = 1;
  ret = rb_thread_call_with_gvl(iodine_protected_call, &task);
  in_gvl = 0;
  return (VALUE)ret;
}

// wrapping any API calls for exception management AND GVL entry
static VALUE iodin_rb_call(VALUE obj, ID method) {
  return iodin_rb_call_arg(obj, method, 0, NULL);
}

////////////////////////////////////////////////////////////////////////////
// the API interface
struct _Ruby_Method_Caller_Class_ RubyCaller = {
    .call = iodin_rb_call,
    .call2 = iodin_rb_call_arg,
    .call_c = iodine_rb_call_c,
    .leave_gvl = iodine_rb_leave_gvl,
    .in_gvl = iodine_rb_check_in_gvl,
    .set_gvl_state = iodine_rb_set_gvl_state,
};
