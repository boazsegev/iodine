#ifndef H___IODINE_THREADS___H
#include "iodine.h"

/* *****************************************************************************
Iodine Threads - Ruby-Aware Threading and Process Management

This module provides Ruby-aware implementations of threading and process
primitives that facil.io uses internally. These implementations ensure
proper interaction with Ruby's Global VM Lock (GVL) and garbage collector.

Key features:
- Process forking via Ruby's Process.fork (preserves Ruby state)
- Thread creation via Ruby's Thread.new (GVL-aware)
- Proper GVL release/acquisition for blocking operations
- Thread-safe signal handling via Ruby's Process.kill

The functions in this module replace the default POSIX implementations
to ensure Ruby compatibility. They're used by facil.io's internal
worker/thread management.

Threading Model:
- Threads are Ruby Thread objects (VALUE) stored as fio_thread_t
- Thread functions run outside the GVL for I/O operations
- GVL is acquired when calling Ruby code

Process Model:
- Workers are forked via Ruby's Process.fork
- Child processes inherit Ruby state properly
- waitpid runs with GVL for proper signal handling
***************************************************************************** */

/* *****************************************************************************
API for Forking Processes
***************************************************************************** */

/**
 * Forks a new process using Ruby's Process.fork.
 *
 * This ensures Ruby state is properly preserved in the child process.
 * Behaves like POSIX fork(): returns 0 in child, PID in parent, -1 on error.
 *
 * @return Child PID in parent, 0 in child, -1 on error
 */
FIO_IFUNC fio_thread_pid_t fio_thread_fork(void) {
  iodine_caller_result_s r =
      iodine_ruby_call_outside(rb_mProcess, rb_intern2("fork", 4), 0, NULL);
  if (r.exception)
    return -1;
  if (r.result == Qnil)
    return 0;
  return NUM2PIDT(r.result);
}

/**
 * Returns the current process ID.
 * Wrapper around fio_getpid() for consistency.
 *
 * @return Current process ID
 */
FIO_IFUNC fio_thread_pid_t fio_thread_getpid(void) {
  return (fio_thread_pid_t)fio_getpid();
}

/**
 * Sends a signal to a process using Ruby's Process.kill.
 *
 * This ensures proper signal handling within Ruby's runtime.
 *
 * @param i Target process ID
 * @param s Signal number to send
 * @return 0 on success, -1 on error
 */
FIO_IFUNC int fio_thread_kill(fio_thread_pid_t i, int s) {
  VALUE args[] = {INT2NUM(s), PIDT2NUM(i)};
  iodine_caller_result_s r = iodine_ruby_call_outside(rb_mProcess,
                                                      rb_intern2("kill", 4),
                                                      2,
                                                      args,
                                                      .ignore_exceptions = 1);
  if (r.exception)
    return -1;
  return 0;
}

typedef struct {
  fio_thread_pid_t pid;
  int *status;
  int flags;
  int ret;
} iodine___wait_pid_args_s;

FIO_SFUNC void *fio___thread_waitpid_in_gvl(void *args_) {
  iodine___wait_pid_args_s *args = (iodine___wait_pid_args_s *)args_;
  args->ret = rb_waitpid(args->pid, args->status, args->flags);
  return NULL;
}

/**
 * Waits for a child process using Ruby's rb_waitpid.
 *
 * Runs with GVL held to ensure proper Ruby signal handling.
 *
 * @param i Process ID to wait for (-1 for any child)
 * @param s Pointer to store exit status
 * @param o Wait options (WNOHANG, etc.)
 * @return Process ID on success, -1 on error
 */
FIO_IFUNC int fio_thread_waitpid(fio_thread_pid_t i, int *s, int o) {
  iodine___wait_pid_args_s args = {i, s, o};
  rb_thread_call_with_gvl(fio___thread_waitpid_in_gvl, (void *)&args);
  return args.ret;
}

/* *****************************************************************************
API for Spawning Threads - Ruby Thread Integration
***************************************************************************** */

typedef struct {
  fio_lock_i lock;
  fio_thread_t *t;
  void *(*fn)(void *);
  void *arg;
} iodine___thread_starter_s;

static VALUE iodine___thread_start_in_gvl(void *args_) {
  iodine___thread_starter_s *args = (iodine___thread_starter_s *)args_;
  iodine___thread_starter_s cpy = *args;
  fio_unlock(&args->lock);
  return (VALUE)rb_thread_call_without_gvl(cpy.fn, cpy.arg, NULL, NULL);
}
static void *iodine___thread_create_in_gvl(void *args_) {
  iodine___thread_starter_s *args = (iodine___thread_starter_s *)args_;
  args->t[0] = rb_thread_create(iodine___thread_start_in_gvl, args_);
  if (args->t[0] == Qnil)
    fio_unlock(&args->lock);
  else
    STORE.hold(args->t[0]);
  return NULL;
}
/**
 * Creates a new thread using Ruby's Thread.new.
 *
 * The thread function runs outside the GVL for I/O operations.
 * The thread is held in STORE to prevent GC until joined/detached.
 *
 * @param t Pointer to store the thread handle (Ruby VALUE)
 * @param fn Thread function to execute
 * @param arg Argument to pass to thread function
 * @return 0 on success, -1 on failure
 */
FIO_IFUNC int fio_thread_create(fio_thread_t *t,
                                void *(*fn)(void *),
                                void *arg) {
  iodine___thread_starter_s starter = {.lock = FIO_LOCK_INIT,
                                       .t = t,
                                       .fn = fn,
                                       .arg = arg};
  fio_lock(&starter.lock);
  rb_thread_call_with_gvl(iodine___thread_create_in_gvl, &starter);
  fio_lock(&starter.lock); /* wait for other thread to unlock */
  if (*starter.t == Qnil)
    goto error_starting_thread;
  return 0;
error_starting_thread:
  FIO_LOG_ERROR("(%d) couldn't start thread!", fio_io_pid());
  return -1;
}

/**
 * Waits for a thread to finish and releases it from STORE.
 *
 * Calls Ruby's Thread#join to wait for completion.
 *
 * @param t Pointer to thread handle
 * @return 0 on success, -1 on error
 */
FIO_IFUNC int fio_thread_join(fio_thread_t *t) {
  STORE.release(t[0]);
  iodine_caller_result_s r =
      iodine_ruby_call_outside(t[0], rb_intern2("join", 4), 0, NULL);
  if (r.exception)
    return -1;
  return 0;
}

/**
 * Detaches a thread, releasing it from STORE.
 *
 * The thread will continue running but resources are freed
 * when it completes.
 *
 * @param t Pointer to thread handle
 * @return Always returns 0
 */
FIO_IFUNC int fio_thread_detach(fio_thread_t *t) {
  STORE.release(t[0]);
  return 0;
}

/**
 * Terminates the current thread.
 *
 * Uses platform-specific exit: pthread_exit on POSIX,
 * _endthread on Windows, rb_thread_kill on other platforms.
 */
FIO_IFUNC void fio_thread_exit(void) {
#if FIO_OS_POSIX
  pthread_exit(NULL);
#elif FIO_OS_WIN
  _endthread();
#else
  rb_thread_kill(rb_thread_current());
#endif
}

/**
 * Compares two thread handles for equality.
 *
 * @param a First thread handle
 * @param b Second thread handle
 * @return Non-zero if threads are the same, 0 otherwise
 */
FIO_IFUNC int fio_thread_equal(fio_thread_t *a, fio_thread_t *b) {
  return *a == *b;
}

/**
 * Returns the current thread handle.
 *
 * @return Current Ruby Thread VALUE
 */
FIO_IFUNC fio_thread_t fio_thread_current(void) { return rb_thread_current(); }

/**
 * Yields execution to other threads.
 *
 * Calls Ruby's Thread.pass to allow other threads to run.
 */
FIO_IFUNC void fio_thread_yield(void) { rb_thread_schedule(); }

#endif /* H___IODINE_THREADS___H */
