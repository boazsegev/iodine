#ifndef H___IODINE_THREADS___H
#include "iodine.h"
/* *****************************************************************************
API for forking processes
***************************************************************************** */

/** Should behave the same as the POSIX system call `fork`. */
FIO_IFUNC fio_thread_pid_t fio_thread_fork(void) {
  iodine_caller_result_s r =
      iodine_ruby_call_outside(rb_mProcess, rb_intern2("fork", 4), 0, NULL);
  if (r.exception)
    return -1;
  if (r.result == Qnil)
    return 0;
  return NUM2INT(r.result);
}

/** Should behave the same as the POSIX system call `getpid`. */
FIO_IFUNC fio_thread_pid_t fio_thread_getpid(void) {
  return (fio_thread_pid_t)getpid();
}

/** Should behave the same as the POSIX system call `kill`. */
FIO_IFUNC int fio_thread_kill(fio_thread_pid_t i, int s) {
  VALUE args[] = {INT2NUM(((int)i)), INT2NUM(s)};
  iodine_caller_result_s r =
      iodine_ruby_call_outside(rb_mProcess, rb_intern2("kill", 4), 2, args);
  if (r.exception)
    return -1;
  return 0;
}

/** Should behave the same as the POSIX system call `waitpid`. */
FIO_IFUNC int fio_thread_waitpid(fio_thread_pid_t i, int *s, int o) {
#if FIO_OS_POSIX
  return waitpid((pid_t)i, s, o);
#else
  return -1;
#endif
}

/* *****************************************************************************
API for spawning threads
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
/** Starts a new thread, returns 0 on success and -1 on failure. */
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
    return -1;
  return 0;
}

/** Waits for the thread to finish. */
FIO_IFUNC int fio_thread_join(fio_thread_t *t) {
  iodine_caller_result_s r =
      iodine_ruby_call_outside(t[0], rb_intern2("join", 4), 0, NULL);
  STORE.release(t[0]);
  if (r.exception)
    return -1;
  return 0;
}

/** Detaches the thread, so thread resources are freed automatically. */
FIO_IFUNC int fio_thread_detach(fio_thread_t *t) {
  STORE.release(t[0]);
  return 0;
}

/** Ends the current running thread. */
FIO_IFUNC void fio_thread_exit(void) {
#if FIO_OS_POSIX
  pthread_exit(NULL);
#elif FIO_OS_WIN
  _endthread();
#else
  rb_thread_kill(rb_thread_current());
#endif
}

/* Returns non-zero if both threads refer to the same thread. */
FIO_IFUNC int fio_thread_equal(fio_thread_t *a, fio_thread_t *b) {
  return *a == *b;
}

/** Returns the current thread. */
FIO_IFUNC fio_thread_t fio_thread_current(void) { return rb_thread_current(); }

/** Yields thread execution. */
FIO_IFUNC void fio_thread_yield(void) { rb_thread_schedule(); }

#endif /* H___IODINE_THREADS___H */