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
#include <fcntl.h>

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
  /// an object mutex (locker).
  pthread_mutex_t locker;
  /// an array to `pthread_t` objects `count` long.
  VALUE thread_pool[];
};

// A task structure.
struct Task {
  void (*task)(void*);
  void* arg;
};

/////////////////////
// Ruby Specific Code
/////////////////////
// the thread loop functions

// a kill switch... We'll need it for the GVL
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

// the main thread loop function
static void* thread_loop_no_gvl(struct Async* async) {
  struct Task task = {};
  signal(SIGPIPE, SIG_IGN);
  if (async->init_thread)
    async->init_thread(async, async->arg);
  int in = async->in;    // keep a copy of the pipe's address on the stack
  int out = async->out;  // keep a copy of the pipe's address on the stack
  while (read(in, &task, sizeof(struct Task)) > 0) {
    if (!task.task) {
      close(in);
      close(out);
      break;
    }
    task.task(task.arg);
  }
  return 0;
}

// the thread's GVL release
static VALUE thread_loop(void* async) {
  rb_thread_call_without_gvl2((void* (*)(void*))thread_loop_no_gvl, async,
                              (void (*)(void*))async_kill, async);
  return Qnil;
}

// Within the GVL, creates a Ruby thread using an API call
static void* create_ruby_thread_gvl(void* async) {
  return (void*)Registry.add(rb_thread_create(thread_loop, async));
}

// create a ruby thread
static VALUE create_rb_thread(struct Async* async) {
  return (VALUE)rb_thread_call_with_gvl(create_ruby_thread_gvl, async);
}

// protect call to join
static void* _inner_join_with_rbthread(void* rbt) {
  return (void*)rb_funcall((VALUE)rbt, rb_intern("join"), 0);
}

// join a ruby thread
static void* join_rb_thread(VALUE thread) {
  return rb_thread_call_with_gvl(_inner_join_with_rbthread, (void*)thread);
}

/////////////////////
// a single task performance, for busy waiting
static int perform_single_task(async_p async) {
  fprintf(stderr,
          "Warning: event queue overloaded!\n"
          "Perfoming out of band tasks, failure could occure.\n"
          "Consider adding process workers of threads for concurrency.\n"
          "\n");
  struct Task task = {};
  if (read(async->in, &task, sizeof(struct Task)) > 0) {
    if (!task.task) {
      close(async->in);
      close(async->out);
      return 0;
    }
    pthread_mutex_unlock(&async->locker);
    task.task(task.arg);
    pthread_mutex_lock(&async->locker);
    return 0;
  } else
    return -1;
}

/////////////////////
// Queue pipe extension management
struct ExtQueueData {
  struct {
    int in;
    int out;
  } io;
  async_p async;
};
static void* extended_queue_thread(void* _data) {
  struct ExtQueueData* data = _data;
  struct Task task;
  int i;
  // get the core out pipe flags (blocking state not important)
  i = fcntl(data->async->out, F_GETFL, NULL);
  // change the original queue writer object to a blocking state
  fcntl(data->io.out, F_SETFL, i & (~O_NONBLOCK));
  // make sure the reader doesn't block
  fcntl(data->io.in, F_SETFL, (O_NONBLOCK));
  while (1) {
    // we're checking the status of our queue, so we don't want stuff to be
    // added while we review.
    pthread_mutex_lock(&data->async->locker);
    i = read(data->io.in, &task, sizeof(struct Task));
    if (i <= 0) {
      // we're done, return the async object to it's previous status
      i = data->async->out;
      data->async->out = data->io.out;
      data->io.out = i;
      // get the core pipe flags (blocking state not important)
      i = fcntl(data->io.out, F_GETFL, NULL);
      // return the writer to a non-blocking state.
      fcntl(data->async->out, F_SETFL, i | O_NONBLOCK);
      // unlock the queue
      pthread_mutex_unlock(&data->async->locker);
      // close the extra pipes
      close(data->io.in);
      close(data->io.out);
      // free the data object
      free(data);
      return 0;
    }
    // unlock the queue - let it be filled.
    pthread_mutex_unlock(&data->async->locker);
    // write to original queue in a blocking manner.
    if (write(data->io.out, &task, sizeof(struct Task)) <= 0) {
      // there was an error while writing - it could be that we're shutting
      // down.
      // close the extra pipes (no need to swap, as all pipes are broken anyway)
      close(data->io.in);
      close(data->io.out);
      free(data);
      return (void*)-1;
    };
  }
}

static int extend_queue(async_p async, struct Task* task) {
  // create the data carrier
  struct ExtQueueData* data = malloc(sizeof(struct ExtQueueData));
  if (!data)
    return -1;
  // create the extra pipes
  if (pipe(&data->io.in)) {
    free(data);
    return -1;
  };
  // set the data's async pointer
  data->async = async;
  // get the core out pipe flags (blocking state not important)
  int flags = fcntl(async->out, F_GETFL, NULL);
  // make the new task writer object non-blocking
  fcntl(data->io.out, F_SETFL, flags | O_NONBLOCK);
  // swap the two writers
  async->out = async->out + data->io.out;
  data->io.out = async->out - data->io.out;
  async->out = async->out - data->io.out;
  // write the task to the new queue - otherwise, our thread might quit before
  // it starts.
  write(async->out, task, sizeof(struct Task));
  // create a thread that listens to the new queue and pushes it to the old
  // queue
  pthread_t thr;
  if (pthread_create(&thr, NULL, extended_queue_thread, data)) {
    // // damn, we were so close to finish... but got an error
    // swap the two writers back
    async->out = async->out + data->io.out;
    data->io.out = async->out - data->io.out;
    async->out = async->out - data->io.out;
    // close
    close(data->io.in);
    close(data->io.out);
    // free
    free(data);
    // inform about the error
    return -1;
  };
  return 0;
}

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

  // create the mutex
  if (pthread_mutex_init(&async->locker, NULL)) {
    close(io[0]);
    close(io[1]);
    free(async);
    return NULL;
  };

  // setup the struct data
  async->count = threads;
  async->init_thread = on_init;
  async->in = io[0];
  async->out = io[1];
  async->arg = arg;

  // make sure write isn't blocking, otherwise we might deadlock.
  // fcntl(async->out, F_SETFL, O_NONBLOCK);

  // create the thread pool
  for (int i = 0; i < threads; i++) {
    if ((async->thread_pool[i] = create_rb_thread(async)) == Qnil) {
      close(io[0]);
      close(io[1]);
      free(async);
      return NULL;
    }
  }
  // prevent pipe issues from stopping the flow of the mmain thread
  signal(SIGPIPE, SIG_IGN);
  // return the pointer
  return async;
}

static int async_run(struct Async* self, void (*task)(void*), void* arg) {
  if (!(task && self))
    return -1;
  struct Task package = {.task = task, .arg = arg};
  int written = 0;
  // "busy" wait for the task buffer to complete tasks by performing tasks in
  // the buffer
  pthread_mutex_lock(&self->locker);
  while ((written = write(self->out, &package, sizeof(struct Task))) !=
         sizeof(struct Task)) {
    if (written > 0) {
      // this is fatal to the Async engine, as a partial write will now mess up
      // all the task-data!  --- This shouldn't be possible because it's all
      // powers of 2. (buffer size is power of 2 and struct size is power of 2).
      fprintf(
          stderr,
          "FATAL: Async queue corruption, cannot continue processing data.\n");
      exit(2);
    }
    if (!extend_queue(self, &package))
      break;
    // closed pipe or other error, return error
    if (perform_single_task(self))
      return -1;
  }
  pthread_mutex_unlock(&self->locker);
  return 0;
}

static void async_signal(struct Async* self) {
  struct Task package = {.task = 0, .arg = 0};
  pthread_mutex_lock(&self->locker);
  while (write(self->out, &package, sizeof(struct Task)) !=
         sizeof(struct Task)) {
    if (!extend_queue(self, &package))
      break;
    // closed pipe, return error
    if (perform_single_task(self))
      break;
  }
  pthread_mutex_unlock(&self->locker);
}

static void async_wait(struct Async* self) {
  for (int i = 0; i < self->count; i++) {
    if (!self->thread_pool[i])
      continue;
    join_rb_thread(self->thread_pool[i]);
    Registry.remove(self->thread_pool[i]);
  }
  close(self->in);
  close(self->out);
  pthread_mutex_destroy(&self->locker);
  free(self);
}

static void async_finish(struct Async* self) {
  async_signal(self);
  async_wait(self);
}

// ////////////
// // debug
// void* tell_us(char const* caller_name, void* arg) {
//   fprintf(stderr, "Run was called from %s\n", caller_name);
//   return arg;
// }

////////////
// the API gateway
const struct AsyncAPI Async = {.new = async_new,
                               .run = async_run,
                               .signal = async_signal,
                               .wait = async_wait,
                               .finish = async_finish,
                               .kill = async_kill};
