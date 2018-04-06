/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
// clang-format off
#include "rb-registry.h"
#include "rb-call.h"
#include "iodine.h"
#include <ruby.h>
#include <ruby/thread.h>

#include <stdint.h>
// clang-format on

#include <spnlock.inc>
#include "defer.h"

#include <pthread.h>

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

static void *defer_thread_start(void *args_) {
  struct CreateThreadArgs *args = args_;
  void *(*thread_func)(void *) = args->thread_func;
  void *arg = args->arg;
  free(args);
  RubyCaller.set_gvl_state(0);
  thread_func(arg);
  return NULL;
}

/* the thread's GVL release */
static VALUE defer_thread_inGVL(void *args_) {
  struct CreateThreadArgs *args = args_;
  rb_thread_call_without_gvl(defer_thread_start, args_,
                             (void (*)(void *))call_async_signal, args->arg);
  return Qnil;
}

/* Within the GVL, creates a Ruby thread using an API call */
static void *create_ruby_thread_gvl(void *args) {
  return (void *)Registry.add(rb_thread_create(defer_thread_inGVL, args));
}

static void *fork_using_ruby(void *ignr) {
  RubyCaller.call(Iodine, rb_intern("before_fork"));
  const VALUE ProcessClass = rb_const_get(rb_cObject, rb_intern("Process"));
  const VALUE rb_pid = RubyCaller.call(ProcessClass, rb_intern("fork"));
  intptr_t pid = 0;
  if (rb_pid != Qnil) {
    pid = NUM2INT(rb_pid);
  } else {
    pid = 0;
  }
  RubyCaller.set_gvl_state(1); /* enforce GVL state in thread storage */
  if (!pid) {
    Registry.on_fork();
    RubyCaller.call(Iodine, rb_intern("after_fork"));
  }
  return (void *)pid;
  (void)ignr;
}

/* *****************************************************************************
The Defer library overriding functions
***************************************************************************** */

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
void *defer_new_thread(void *(*thread_func)(void *), void *arg) {
  struct CreateThreadArgs *data = malloc(sizeof(*data));
  if (!data)
    return NULL;
  *data = (struct CreateThreadArgs){
      .thread_func = thread_func, .arg = arg,
  };
  void *thr = RubyCaller.call_c(create_ruby_thread_gvl, data);
  if (!thr || thr == (void *)Qnil || thr == (void *)Qfalse) {
    thr = NULL;
  }
  return thr;
}

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
int defer_join_thread(void *thr) {
  if (!thr || (VALUE)thr == Qfalse || (VALUE)thr == Qnil)
    return -1;
  RubyCaller.call((VALUE)thr, rb_intern("join"));
  Registry.remove((VALUE)thr);
  return 0;
}

// void defer_free_thread(void *thr) { (void)thr; }
void defer_free_thread(void *thr) { Registry.remove((VALUE)thr); }

/**
OVERRIDE THIS to replace the default `fork` implementation or to inject hooks
into the forking function.

Behaves like the system's `fork`.
*/
int facil_fork(void) {
  intptr_t pid = (intptr_t)RubyCaller.call_c(fork_using_ruby, NULL);
  return (int)pid;
}
