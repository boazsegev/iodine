#ifndef H___IODINE_CALLER___H
#define H___IODINE_CALLER___H
#include "iodine.h"

/* *****************************************************************************
Ruby Caller and Thread (GVL) Helper

From within the GVL call Ruby functions so:

    iodine_caller_result_s r = iodine_ruby_call_inside(recv, mid,
                                                       argc, argv, proc);

From outside the GVL call Ruby functions so:

    iodine_caller_result_s r = iodine_ruby_call_outside(recv, mid, argc, argv);

***************************************************************************** */

/* printout backtrace in case of exceptions */
static void *iodine_handle_exception(void *ignr) {
  (void)ignr;
  FIO_LOG_ERROR("iodine catching an exposed exception");
  VALUE exc = rb_errinfo();
  if (exc != Qnil && rb_respond_to(exc, rb_intern("message")) &&
      rb_respond_to(exc, rb_intern("backtrace"))) {
    VALUE msg = rb_funcallv(exc, rb_intern("message"), 0, NULL);
    VALUE exc_class = rb_class_name(CLASS_OF(exc));
    VALUE bt = rb_funcallv(exc, rb_intern("backtrace"), 0, NULL);
    if (msg == Qnil)
      msg = rb_str_new("Error message unavailable", 25);
    if (exc_class == Qnil)
      exc_class = rb_str_new("unknown exception class", 23);
    if (TYPE(bt) == RUBY_T_ARRAY) {
      bt = rb_ary_join(bt, rb_str_new_literal("\n"));
      FIO_LOG_ERROR("Iodine caught an unprotected exception - %.*s: %.*s\n %s ",
                    (int)RSTRING_LEN(exc_class),
                    RSTRING_PTR(exc_class),
                    (int)RSTRING_LEN(msg),
                    RSTRING_PTR(msg),
                    StringValueCStr(bt));
    } else if (TYPE(bt) == RUBY_T_STRING) {
      FIO_LOG_ERROR("Iodine caught an unprotected exception - %.*s: %.*s\n"
                    "No backtrace available.\n",
                    (int)RSTRING_LEN(exc_class),
                    RSTRING_PTR(exc_class),
                    (int)RSTRING_LEN(msg),
                    RSTRING_PTR(msg));
    } else {
      FIO_LOG_ERROR("Iodine caught an unprotected exception - %.*s: %.*s\n %s\n"
                    "BACKTRACE UNAVAILABLE!\n",
                    (int)RSTRING_LEN(exc_class),
                    RSTRING_PTR(exc_class),
                    (int)RSTRING_LEN(msg),
                    RSTRING_PTR(msg));
      FIO_LOG_ERROR("Backtrace missing.");
    }
    rb_backtrace();
    FIO_LOG_ERROR("\n");
    rb_set_errinfo(Qnil);
  } else if (exc != Qnil) {
    FIO_LOG_ERROR(
        "Iodine caught an unprotected exception - NO MESSAGE / DATA AVAILABLE");
  }
  return (void *)Qnil;
}

typedef struct {
  VALUE recv;
  ID mid;
  int argc;
  VALUE *argv;
  VALUE proc;
} iodine_caller_args_s;

typedef struct {
  VALUE result;
  int exception;
} iodine_caller_result_s;

typedef struct {
  iodine_caller_args_s in;
  iodine_caller_result_s out;
} iodine___caller_s;

static VALUE iodine___func_caller_task(VALUE args_) {
  iodine_caller_args_s *args = (iodine_caller_args_s *)args_;
  return rb_funcallv(args->recv, args->mid, args->argc, args->argv);
}

static VALUE iodine___func_caller_task_proc(VALUE args_) {
  iodine_caller_args_s *args = (iodine_caller_args_s *)args_;
  return rb_funcall_with_block(args->recv,
                               args->mid,
                               args->argc,
                               args->argv,
                               args->proc);
}

static void *iodine_ruby____outside_task_proc(void *c_) {
  iodine___caller_s *c = (iodine___caller_s *)c_;
  c->out.result = rb_protect(iodine___func_caller_task_proc,
                             (VALUE)&c->in,
                             &c->out.exception);
  if (c->out.exception)
    iodine_handle_exception(NULL);
  return NULL;
}

static void *iodine_ruby____outside_task(void *c_) {
  iodine___caller_s *c = (iodine___caller_s *)c_;
  c->out.result =
      rb_protect(iodine___func_caller_task, (VALUE)&c->in, &c->out.exception);
  if (c->out.exception)
    iodine_handle_exception(NULL);
  return NULL;
}

/* *****************************************************************************
Ruby Caller and Thread (GVL) - Helper Implementation
***************************************************************************** */

inline static iodine_caller_result_s iodine_ruby_call_inside(
    iodine_caller_args_s args) {
  iodine_caller_result_s r = {0};
  FIO_ASSERT_DEBUG(args.recv && args.mid,
                   "iodine_ruby_call requires an object and method name");
  VALUE stub[1] = {Qnil};
  if (!args.argv)
    args.argv = stub;
  r.result = rb_protect(
      (args.proc ? iodine___func_caller_task_proc : iodine___func_caller_task),
      (VALUE)&args,
      &r.exception);
  if (r.exception)
    iodine_handle_exception(NULL);
  return r;
}

inline static iodine_caller_result_s iodine_ruby_call_outside(
    iodine_caller_args_s args) {
  VALUE stub[1] = {Qnil};
  if (!args.argv)
    args.argv = stub;
  iodine___caller_s r = {args, {0}};
  FIO_ASSERT_DEBUG(args.recv && args.mid,
                   "iodine_ruby_call requires an object and method name");
  rb_thread_call_with_gvl(args.proc ? iodine_ruby____outside_task_proc
                                    : iodine_ruby____outside_task,
                          &r);
  return r.out;
}

/**

Accept the following, possibly named, arguments:

  (VALUE recv,  ID mid,  int argc,  VALUE *argv,  VALUE proc)

*/
#define iodine_ruby_call_inside(...)                                           \
  iodine_ruby_call_inside((iodine_caller_args_s){__VA_ARGS__})

/**

Accept the following, possibly named, arguments:

  (VALUE recv,  ID mid,  int argc,  VALUE *argv,  VALUE proc)

*/
#define iodine_ruby_call_outside(...)                                          \
  iodine_ruby_call_outside((iodine_caller_args_s){__VA_ARGS__})

#endif /* H___IODINE_CALLER___H */
