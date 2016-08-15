/*
copyright: Boaz segev, 2016
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef RB_ASYNC_EXT_H
#define RB_ASYNC_EXT_H
#include <ruby.h>
#include <ruby/thread.h>
#include "rb-registry.h"

/******************************************************************************
Portability - used to help port this to different frameworks (i.e. Ruby).
*/

#define THREAD_TYPE VALUE

/* Don't use sentinals with Ruby */
#ifndef ASYNC_USE_SENTINEL
#define ASYNC_USE_SENTINEL 0
#endif

/* The unused directive */
#ifndef __unused
#define __unused __attribute__((unused))
#endif

/* used here but declared elsewhere */
void async_signal();

/* used here but declared elsewhere */
void call_async_signal(void *_) { async_signal(); }

/* protect the call to join from any exceptions */
static void *_inner_join_with_rbthread(void *rbt) {
  return (void *)rb_funcall((VALUE)rbt, rb_intern("join"), 0);
}

/* join a ruby thread */
__unused static void *join_thread(THREAD_TYPE thr) {
  void *ret = rb_thread_call_with_gvl(_inner_join_with_rbthread, (void *)thr);
  Registry.remove(thr);
  return ret;
}
/* used to create Ruby threads and pass them the information they need */
struct CreateThreadArgs {
  void *(*thread_func)(void *);
  void *arg;
};

/* the thread's GVL release */
static VALUE thread_loop(void *_args) {
  struct CreateThreadArgs *args = _args;
  void *(*thread_func)(void *) = args->thread_func;
  void *arg = args->arg;
  free(_args);
  rb_thread_call_without_gvl2(thread_func, arg,
                              (void (*)(void *))call_async_signal, arg);
  return Qnil;
}

/* Within the GVL, creates a Ruby thread using an API call */
static void *create_ruby_thread_gvl(void *_args) {
  return (void *)Registry.add(rb_thread_create(thread_loop, _args));
}

/* create a ruby thread */
__unused static int create_thread(THREAD_TYPE *thr,
                                  void *(*thread_func)(void *), void *arg) {
  struct CreateThreadArgs *data = malloc(sizeof(*data));
  if (!data)
    return -1;
  *data = (struct CreateThreadArgs){.thread_func = thread_func, .arg = arg};
  *thr = (VALUE)rb_thread_call_with_gvl(create_ruby_thread_gvl, data);
  return *thr == Qnil;
}

#endif
