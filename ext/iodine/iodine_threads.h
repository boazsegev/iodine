#ifndef H___IODINE_THREADS___H
#include "iodine.h"

/* *****************************************************************************
Condition Variables - BYO Implementation (FIO_THREADS_COND_BYO)

Provides Windows and POSIX implementations of condition variable primitives
used by facil.io's worker thread pool.

On Windows: Uses SleepConditionVariableSRW with INFINITE timeout, matching
POSIX pthread_cond_wait behavior. Workers are properly signaled when:
  - Tasks are added to the queue (fio_queue_push signals all consumers)
  - Shutdown is initiated (fio_queue_workers_stop signals all workers)

On POSIX: Delegates to pthread_cond_* unchanged.
***************************************************************************** */

#ifdef _WIN32

/* fio_thread_cond_t is typedef'd as CONDITION_VARIABLE in iodine.h */

FIO_IFUNC int fio_thread_cond_init(fio_thread_cond_t *c) {
  InitializeConditionVariable(c);
  return 0;
}

/* Wait indefinitely until signaled — matches POSIX pthread_cond_wait behavior.
 * Workers are signaled when tasks are added or during shutdown. */
FIO_IFUNC int fio_thread_cond_wait(fio_thread_cond_t *c,
                                   fio_thread_mutex_t *m) {
  SleepConditionVariableSRW(c, m, INFINITE, 0);
  return 0;
}

FIO_IFUNC int fio_thread_cond_timedwait(fio_thread_cond_t *c,
                                        fio_thread_mutex_t *m,
                                        size_t milliseconds) {
  return 0 - !SleepConditionVariableSRW(c, m, (DWORD)milliseconds, 0);
}

FIO_IFUNC int fio_thread_cond_signal(fio_thread_cond_t *c) {
  WakeConditionVariable(c);
  return 0;
}

FIO_IFUNC void fio_thread_cond_destroy(fio_thread_cond_t *c) { (void)c; }

#else /* POSIX */

/* fio_thread_cond_t is typedef'd as pthread_cond_t in iodine.h */

FIO_IFUNC int fio_thread_cond_init(fio_thread_cond_t *c) {
  return pthread_cond_init(c, NULL);
}

FIO_IFUNC int fio_thread_cond_wait(fio_thread_cond_t *c,
                                   fio_thread_mutex_t *m) {
  return pthread_cond_wait(c, m);
}

FIO_IFUNC int fio_thread_cond_timedwait(fio_thread_cond_t *c,
                                        fio_thread_mutex_t *m,
                                        size_t milliseconds) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  t.tv_sec += (time_t)(milliseconds / 1000);
  t.tv_nsec += (long)((milliseconds % 1000) * 1000000);
  if (t.tv_nsec >= 1000000000L) {
    t.tv_sec++;
    t.tv_nsec -= 1000000000L;
  }
  return pthread_cond_timedwait(c, m, &t);
}

FIO_IFUNC int fio_thread_cond_signal(fio_thread_cond_t *c) {
  return pthread_cond_signal(c);
}

FIO_IFUNC void fio_thread_cond_destroy(fio_thread_cond_t *c) {
  pthread_cond_destroy(c);
}

#endif /* _WIN32 / POSIX */

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
      iodine_ruby_call_anywhere(rb_mProcess, rb_intern2("fork", 4), 0, NULL);
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
  iodine_caller_result_s r = iodine_ruby_call_anywhere(rb_mProcess,
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
  iodine_c_call_with(fio___thread_waitpid_in_gvl, (void *)&args);
  return args.ret;
}

/* *****************************************************************************
API for Spawning Threads - Ruby Thread Integration
***************************************************************************** */

#ifdef _WIN32
typedef struct {
  VALUE thread;
  HANDLE wait_handle;
  uint32_t refs;
} iodine___thread_handle_s;

static void iodine___thread_handle_release(iodine___thread_handle_s *handle) {
  if (fio_atomic_sub(&handle->refs, 1) != 1)
    return;
  CloseHandle(handle->wait_handle);
  FIO_MEM_FREE_(handle, sizeof(*handle));
}

typedef struct {
  HANDLE handle;
  DWORD result;
  DWORD error;
} iodine___thread_wait_args_s;

static void *iodine___thread_wait_without_gvl(void *args_) {
  iodine___thread_wait_args_s *args = (iodine___thread_wait_args_s *)args_;
  args->result = WaitForSingleObject(args->handle, INFINITE);
  args->error = args->result == WAIT_OBJECT_0 ? 0 : GetLastError();
  return NULL;
}
#endif

typedef struct {
  fio_lock_i lock;
  fio_thread_t *t;
  void *(*fn)(void *);
  void *arg;
#ifdef _WIN32
  iodine___thread_handle_s *handle;
#endif
} iodine___thread_starter_s;

#ifdef _WIN32
static VALUE iodine___thread_complete(VALUE handle_) {
  iodine___thread_handle_s *handle =
      (iodine___thread_handle_s *)(uintptr_t)handle_;
  if (!SetEvent(handle->wait_handle))
    FIO_LOG_ERROR("(%d) couldn't signal thread completion!", fio_io_pid());
  iodine___thread_handle_release(handle);
  return Qnil;
}
#endif

static VALUE iodine___thread_run_without_gvl(VALUE args_) {
  iodine___thread_starter_s *args = (iodine___thread_starter_s *)args_;
  return (VALUE)iodine_c_call_without(args->fn, args->arg);
}

static VALUE iodine___thread_start_in_gvl(void *args_) {
  iodine___thread_starter_s *args = (iodine___thread_starter_s *)args_;
  iodine___thread_starter_s cpy = *args;
  fio_unlock(&args->lock);
#ifdef _WIN32
  return rb_ensure(iodine___thread_run_without_gvl,
                   (VALUE)&cpy,
                   iodine___thread_complete,
                   (VALUE)(uintptr_t)cpy.handle);
#else
  return iodine___thread_run_without_gvl((VALUE)&cpy);
#endif
}

static void *iodine___thread_create_in_gvl(void *args_) {
  iodine___thread_starter_s *args = (iodine___thread_starter_s *)args_;
  VALUE thread = rb_thread_create(iodine___thread_start_in_gvl, args_);
#ifdef _WIN32
  args->handle->thread = thread;
#else
  args->t[0] = thread;
#endif
  if (thread == Qnil)
    fio_unlock(&args->lock);
  else
    STORE.hold(thread);
  return NULL;
}

/**
 * Creates a new Ruby thread whose facil.io task runs outside the GVL.
 *
 * The Ruby Thread is held in STORE until joined/detached. Windows additionally
 * uses a completion event so queue-manager threads never need to re-enter Ruby
 * merely to wait for another thread.
 */
FIO_IFUNC int fio_thread_create(fio_thread_t *t,
                                void *(*fn)(void *),
                                void *arg) {
#ifdef _WIN32
  iodine___thread_handle_s *handle = (iodine___thread_handle_s *)
      FIO_MEM_REALLOC_(NULL, 0, sizeof(*handle), 0);
  if (!handle)
    goto error_starting_thread;
  *handle = (iodine___thread_handle_s){.thread = Qnil, .refs = 2};
  handle->wait_handle = CreateEventA(NULL, TRUE, FALSE, NULL);
  if (!handle->wait_handle) {
    FIO_MEM_FREE_(handle, sizeof(*handle));
    goto error_starting_thread;
  }
  *t = (fio_thread_t)(uintptr_t)handle;
  iodine___thread_starter_s starter = {.lock = FIO_LOCK_INIT,
                                       .t = t,
                                       .fn = fn,
                                       .arg = arg,
                                       .handle = handle};
#else
  iodine___thread_starter_s starter = {.lock = FIO_LOCK_INIT,
                                       .t = t,
                                       .fn = fn,
                                       .arg = arg};
#endif
  fio_lock(&starter.lock);
  iodine_c_call_with(iodine___thread_create_in_gvl, &starter);
  fio_lock(&starter.lock); /* wait for other thread to copy starter */
#ifdef _WIN32
  if (handle->thread == Qnil) {
    CloseHandle(handle->wait_handle);
    FIO_MEM_FREE_(handle, sizeof(*handle));
    *t = 0;
    goto error_starting_thread;
  }
#else
  if (*starter.t == Qnil)
    goto error_starting_thread;
#endif
  return 0;
error_starting_thread:
  FIO_LOG_ERROR("(%d) couldn't start thread!", fio_io_pid());
  return -1;
}

/** Waits for a thread to finish and releases its STORE hold. */
FIO_IFUNC int fio_thread_join(fio_thread_t *t) {
#ifdef _WIN32
  iodine___thread_handle_s *handle =
      (iodine___thread_handle_s *)(uintptr_t)t[0];
  if (!handle)
    return -1;
  iodine___thread_wait_args_s args = {
      .handle = handle->wait_handle, .result = WAIT_FAILED, .error = 0};
  iodine_c_call_without(iodine___thread_wait_without_gvl, &args);
  int failed = args.result != WAIT_OBJECT_0;
  STORE.release(handle->thread);
  *t = 0;
  iodine___thread_handle_release(handle);
  if (failed) {
    errno = (int)args.error;
    return -1;
  }
  return 0;
#else
  /* The handle pointer may belong to the joining thread's stack and become
   * invalid as soon as join returns. Copy and root the VALUE until then. */
  fio_thread_t thread = t[0];
  iodine_caller_result_s r =
      iodine_ruby_call_anywhere(thread, IODINE_JOIN_ID, 0, NULL);
  STORE.release(thread);
  return r.exception ? -1 : 0;
#endif
}

/** Detaches a thread and releases its STORE hold. */
FIO_IFUNC int fio_thread_detach(fio_thread_t *t) {
#ifdef _WIN32
  iodine___thread_handle_s *handle =
      (iodine___thread_handle_s *)(uintptr_t)t[0];
  if (!handle)
    return -1;
  STORE.release(handle->thread);
  *t = 0;
  iodine___thread_handle_release(handle);
#else
  STORE.release(t[0]);
#endif
  return 0;
}

/**
 * Terminates the current thread via Ruby's thread lifecycle.
 *
 * All iodine threads are Ruby Thread objects. They must exit through Ruby's
 * own lifecycle so that blocking-region state (th->blocking, th->unblock) is
 * properly unwound. rb_thread_kill requires the GVL; fio_thread_exit may be
 * called without it, so we acquire the GVL via iodine_c_call_with first.
 *
 * Inside the GVL, rb_thread_current() is safe and returns the correct VALUE
 * for the calling thread. Note: this function is currently dead code —
 * facil.io thread workers exit by returning NULL from their thread function.
 */
static void *fio___thread_exit_in_gvl(void *ignr) {
  (void)ignr;
  rb_thread_kill(rb_thread_current());
  return NULL;
}

FIO_IFUNC void fio_thread_exit(void) {
  iodine_c_call_with(fio___thread_exit_in_gvl, NULL);
}

/** Compares two Ruby Thread handles for equality. */
FIO_IFUNC int fio_thread_equal(fio_thread_t *a, fio_thread_t *b) {
#ifdef _WIN32
  iodine___thread_handle_s *left =
      (iodine___thread_handle_s *)(uintptr_t)a[0];
  iodine___thread_handle_s *right =
      (iodine___thread_handle_s *)(uintptr_t)b[0];
  return left && right && left->thread == right->thread;
#else
  return *a == *b;
#endif
}

/** Returns the current Ruby thread as a join-compatible handle. */
FIO_IFUNC fio_thread_t fio_thread_current(void) {
  iodine_caller_result_s r =
      iodine_ruby_call_anywhere(rb_cThread, IODINE_CURRENT_ID, 0, NULL);
#ifdef _WIN32
  if (r.exception)
    return 0;
  iodine___thread_handle_s *handle = (iodine___thread_handle_s *)
      FIO_MEM_REALLOC_(NULL, 0, sizeof(*handle), 0);
  if (!handle)
    return 0;
  *handle = (iodine___thread_handle_s){.thread = r.result, .refs = 1};
  if (!DuplicateHandle(GetCurrentProcess(),
                       GetCurrentThread(),
                       GetCurrentProcess(),
                       &handle->wait_handle,
                       SYNCHRONIZE,
                       FALSE,
                       0)) {
    FIO_MEM_FREE_(handle, sizeof(*handle));
    return 0;
  }
  STORE.hold(handle->thread);
  return (fio_thread_t)(uintptr_t)handle;
#else
  return (fio_thread_t)r.result;
#endif
}

/** Returns a process-local numeral ID for the current thread. */
FIO_IFUNC uintptr_t fio_thread_nid(void) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) ||        \
    defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) ||   \
    defined(__sun) || defined(_AIX)
  return (uintptr_t)pthread_self();
#elif defined(_WIN32)
  return (uintptr_t)GetCurrentThreadId();
#else
  /* errno is thread-local on POSIX systems and avoids pthread_t assumptions. */
  return (uintptr_t)&errno;
#endif
}

/**
 * Yields execution to other threads.
 *
 * Called without the GVL — uses OS-level yield, NOT rb_thread_schedule()
 * which requires the GVL.
 */
FIO_IFUNC void fio_thread_yield(void) {
#ifdef _WIN32
  Sleep(0);
#else
  sched_yield();
#endif
}

#endif /* H___IODINE_THREADS___H */
