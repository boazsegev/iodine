/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
// clang-format off
#include "rb-registry.h"
#include <ruby.h>
#include <ruby/thread.h>
// clang-format on

#include "defer.h"

/* *****************************************************************************
Local helpers
***************************************************************************** */
/* used to create Ruby threads and pass them the information they need */
struct CreateThreadArgs {
  void *(*thread_func)(void *);
  void *arg;
};

/* used here but declared elsewhere */
void call_async_signal(void *pool) { defer_pool_stop((pool_pt)pool); }

/* the thread's GVL release */
static VALUE thread_loop(void *args_) {
  struct CreateThreadArgs *args = args_;
  void *(*thread_func)(void *) = args->thread_func;
  void *arg = args->arg;
  free(args_);
  rb_thread_call_without_gvl2(thread_func, arg,
                              (void (*)(void *))call_async_signal, arg);
  return Qnil;
}

/* Within the GVL, creates a Ruby thread using an API call */
static void *create_ruby_thread_gvl(void *args) {
  return (void *)Registry.add(rb_thread_create(thread_loop, args));
}

/* protect the call to join from any exceptions */
static void *_inner_join_with_rbthread(void *rbt) {
  return (void *)rb_funcall((VALUE)rbt, rb_intern("join"), 0);
}

/* *****************************************************************************
The Defer library overriding functions
***************************************************************************** */

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
void *defer_new_thread(void *(*thread_func)(void *), pool_pt pool) {
  struct CreateThreadArgs *data = malloc(sizeof(*data));
  if (!data)
    return NULL;
  *data = (struct CreateThreadArgs){.thread_func = thread_func, .arg = pool};
  void *thr = rb_thread_call_with_gvl(create_ruby_thread_gvl, data);
  if (!thr || thr == (void *)Qnil || thr == (void *)Qfalse)
    thr = NULL;
  return thr;
}

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
int defer_join_thread(void *thr) {
  if (!thr || (VALUE)thr == Qfalse || (VALUE)thr == Qnil)
    return -1;
  rb_thread_call_with_gvl(_inner_join_with_rbthread, (void *)thr);
  Registry.remove((VALUE)thr);
  return 0;
}
