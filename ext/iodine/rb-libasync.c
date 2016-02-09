/*
copyright: Boaz segev, 2015
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "libasync.h"
#include "rb-registry.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <execinfo.h>

#include <ruby.h>
#include <ruby/thread.h>

// // why can't I fund these?!
// extern int rb_thread_join(VALUE thread, double limit);
// #define DELAY_INFTY 1E30
// so I will revert to rb_funcall(thread, rb_intern("join"), 0); //?

////////////////////////////////////////////////////////////////////////////////
// This version of the libasync library is adjusted for Ruby extensions
//
// This version discards the sentinal thread(!), assuming the C code will run
// correctly.
//
// Instead of native POSIX threads, this library creates Ruby threads that work
// WITHOUT the GVL - so that Ruby API calls will require the
// `rb_thread_call_with_gvl` function to be called first.
//
// ... On the other, these threads are allowed to call Ruby API when using
// `rb_thread_call_with_gvl`, which isn't true for POSIX threads.

////////////////////
// types used

struct Async {
  /// an array to `pthread_t` objects `count` long.
  VALUE* thread_pool;
  /// the number of threads in the array.
  int count;
  /// The read only part of the pipe used to push tasks.
  int in;
  /// The write only part of the pipe used to push tasks.
  int out;
  /// a callback used whenever a new thread a spawned.
  void (*init_thread)(struct Async*, void*);
  /// a pointer for the callback.
  void* arg;
};

// A task structure.
struct Task {
  void (*task)(void*);
  void* arg;
};

/////////////////////
// kill switch...
// We'll need it for the GVL
static void async_kill(struct Async* self) {
  struct Task package = {.task = 0, .arg = 0};
  if (write(self->out, &package, sizeof(struct Task)))
    ;
  close(self->in);
  close(self->out);
  for (int i = 0; i < self->count; i++) {
    Registry.remove(self->thread_pool[i]);
  }
  // free(self); // a leak is better then double freeing once finish is called.
}

/////////////////////
// the thread loop functions
// the main thread loop function
static void* thread_loop_no_gvl(struct Async* async) {
  struct Task task = {};
  signal(SIGPIPE, SIG_IGN);
  if (async->init_thread)
    async->init_thread(async, async->arg);
  int in = async->in;
  while (read(in, &task, sizeof(struct Task)) > 0) {
    if (!task.task)
      break;
    task.task(task.arg);
  }
  close(in);
  return 0;
}
// the thread's GVL release
VALUE thread_loop(void* async) {
  rb_thread_call_without_gvl2((void* (*)(void*))thread_loop_no_gvl, async,
                              (void (*)(void*))async_kill, async);
  return Qnil;
}

// creates a Ruby tread using an API call (requires GVL)
void* create_ruby_thread_gvl(void* async) {
  return (void*)Registry.add(rb_thread_create(thread_loop, async));
}

/////////////////////
// the functions

// creates a new aync object
static struct Async* async_new(int threads,
                               void (*on_init)(struct Async* self, void* arg),
                               void* arg) {
  if (threads <= 0)
    return NULL;
  // create the tasking pipe.
  int io[2];
  if (pipe(io))
    return NULL;

  // allocate the memory
  size_t memory_required =
      sizeof(struct Async) + (sizeof(VALUE) * (threads + 1));
  struct Async* async = malloc(memory_required);
  if (!async) {
    close(io[0]);
    close(io[1]);
    return NULL;
  }
  // setup the struct data
  async->count = threads;
  async->init_thread = on_init;
  async->thread_pool = (void*)(async + 1);
  async->in = io[0];
  async->out = io[1];
  async->arg = arg;
  // // testing pipes
  // {
  //   char tstsrt[] = "hithere";
  //   char re[3] = {};
  //   fprintf(stderr, "testing pipes\n");
  //   fprintf(stderr, "write\n");
  //   write(async->out, tstsrt, 2);
  //   fprintf(stderr, "read\n");
  //   read(async->in, re, 2);
  //   fprintf(stderr, "got %s\n", re);
  // }
  // create the thread pool
  for (int i = 0; i < threads; i++) {
    if ((async->thread_pool[i] = (VALUE)rb_thread_call_with_gvl(
             create_ruby_thread_gvl, async)) == Qnil) {
      close(io[0]);
      close(io[1]);
      free(async);
      return NULL;
    }
  }
  // return the pointer
  return async;
}
static int async_run(struct Async* self, void (*task)(void*), void* arg) {
  if (!(task && self))
    return -1;
  struct Task package = {.task = task, .arg = arg};
  return write(self->out, &package, sizeof(struct Task));
}

//////////////////////
// protect call to join
void* join_with_rbthread(void* rbt) {
  return (void*)rb_funcall((VALUE)rbt, rb_intern("join"), 0);
}

////////////
// API gateway

static void async_signal(struct Async* self) {
  struct Task package = {.task = 0, .arg = 0};
  if (write(self->out, &package, sizeof(struct Task)))
    ;
}
static void async_wait(struct Async* self) {
  for (int i = 0; i < self->count; i++) {
    if (!self->thread_pool[i])
      continue;
    rb_thread_call_with_gvl(join_with_rbthread, (void*)self->thread_pool[i]);
    Registry.remove(self->thread_pool[i]);
  }
  close(self->in);
  close(self->out);
  free(self);
}

static void async_finish(struct Async* self) {
  async_signal(self);
  async_wait(self);
}

////////////
// API gateway

// the API gateway
const struct AsyncAPI Async = {.new = async_new,
                               .run = async_run,
                               .signal = async_signal,
                               .wait = async_wait,
                               .finish = async_finish,
                               .kill = async_kill};
