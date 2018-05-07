#include "iodine.h"

#include <ruby/thread.h>

#include <stdint.h>
// clang-format on

#include "facil.h"
#include <spnlock.inc>

#include <pthread.h>

/* *****************************************************************************
IO flushing dedicated thread for protection against blocking code
***************************************************************************** */

static spn_lock_i sock_io_thread = 0;
static pthread_t sock_io_pthread;
typedef struct {
  size_t threads;
  size_t processes;
} iodine_start_settings_s;

static void *iodine_io_thread(void *arg) {
  (void)arg;
  struct timespec tm;
  while (sock_io_thread) {
    sock_flush_all();
    tm = (struct timespec){.tv_nsec = 0, .tv_sec = 1};
    nanosleep(&tm, NULL);
  }
  return NULL;
}
static void iodine_start_io_thread(void *a_, void *b_) {
  if (!spn_trylock(&sock_io_thread)) {
    pthread_create(&sock_io_pthread, NULL, iodine_io_thread, NULL);
  }
  (void)a_;
  (void)b_;
}
static void iodine_join_io_thread(void) {
  if (spn_unlock(&sock_io_thread)) {
    sock_io_thread = 0;
    pthread_join(sock_io_pthread, NULL);
    sock_io_pthread = NULL;
  }
}

/* *****************************************************************************
The Defer library overriding functions
***************************************************************************** */

/* used to create Ruby threads and pass them the information they need */
struct CreateThreadArgs {
  void *(*thread_func)(void *);
  void *arg;
  spn_lock_i lock;
};

/* used for GVL signalling */
void call_async_signal(void *pool) { defer_pool_stop((pool_pt)pool); }

static void *defer_thread_start(void *args_) {
  struct CreateThreadArgs *args = args_;
  IodineCaller.set_GVL(0);
  args->thread_func(args->arg);
  return NULL;
}

/* the thread's GVL release */
static VALUE defer_thread_inGVL(void *args_) {
  struct CreateThreadArgs *old_args = args_;
  struct CreateThreadArgs args = *old_args;
  IodineCaller.set_GVL(1);
  spn_unlock(&old_args->lock);
  rb_thread_call_without_gvl(defer_thread_start, &args,
                             (void (*)(void *))call_async_signal, args.arg);
  return Qnil;
}

/* Within the GVL, creates a Ruby thread using an API call */
static void *create_ruby_thread_gvl(void *args) {
  return (void *)IodineStore.add(rb_thread_create(defer_thread_inGVL, args));
}

/* Runs the before / after fork callbacks (if `before` is true, before runs) */
static void iodine_perform_fork_callbacks(uint8_t before);

static void *fork_using_ruby(void *ignr) {
  // stop IO thread and call before_fork callbacks
  if (sock_io_pthread) {
    iodine_join_io_thread();
  }
  iodine_perform_fork_callbacks(1);
  // fork
  const VALUE ProcessClass = rb_const_get(rb_cObject, rb_intern2("Process", 7));
  const VALUE rb_pid = IodineCaller.call(ProcessClass, rb_intern2("fork", 4));
  intptr_t pid = 0;
  if (rb_pid != Qnil) {
    pid = NUM2INT(rb_pid);
  } else {
    pid = 0;
  }
  // manage post forking state
  IodineCaller.set_GVL(1); /* enforce GVL state in thread storage */
  if (!pid) {
    IodineStore.after_fork();
  }
  iodine_perform_fork_callbacks(0);
  // re-initiate IO thread
  defer(iodine_start_io_thread, NULL, NULL);
  return (void *)pid;
  (void)ignr;
}

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
void *defer_new_thread(void *(*thread_func)(void *), void *arg) {
  struct CreateThreadArgs data = (struct CreateThreadArgs){
      .thread_func = thread_func, .arg = arg, .lock = SPN_LOCK_INIT,
  };
  spn_lock(&data.lock);
  void *thr = IodineCaller.enterGVL(create_ruby_thread_gvl, &data);
  if (!thr || thr == (void *)Qnil || thr == (void *)Qfalse) {
    thr = NULL;
  } else {
    /* wait for thread to signal it's alive. */
    spn_lock(&data.lock);
  }
  return thr;
}

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
int defer_join_thread(void *thr) {
  if (!thr || (VALUE)thr == Qfalse || (VALUE)thr == Qnil)
    return -1;
  IodineCaller.call((VALUE)thr, rb_intern("join"));
  IodineStore.remove((VALUE)thr);
  return 0;
}

// void defer_free_thread(void *thr) { (void)thr; }
void defer_free_thread(void *thr) { IodineStore.remove((VALUE)thr); }

/**
OVERRIDE THIS to replace the default `fork` implementation or to inject hooks
into the forking function.

Behaves like the system's `fork`.
*/
int facil_fork(void) {
  intptr_t pid = (intptr_t)IodineCaller.enterGVL(fork_using_ruby, NULL);
  return (int)pid;
}

/* *****************************************************************************
Task performance
***************************************************************************** */

static ID call_id;

static void iodine_defer_performe_once(void *block, void *ignr) {
  IodineCaller.call((VALUE)block, call_id);
  IodineStore.remove((VALUE)block);
  (void)ignr;
}

static void iodine_defer_run_timer(void *block) {
  IodineCaller.call((VALUE)block, call_id);
}

/* *****************************************************************************
Defer API
***************************************************************************** */

/**
 * Runs a block of code asyncronously (adds the code to the event queue).
 *
 * Always returns the block of code to executed (Proc object).
 *
 * Code will be executed only while Iodine is running (after {Iodine.start}).
 *
 * Code blocks that where scheduled to run before Iodine enters cluster mode
 * will run on all child processes.
 */
static VALUE iodine_defer_run(VALUE self) {
  rb_need_block();
  VALUE block = IodineStore.add(rb_block_proc());
  defer(iodine_defer_performe_once, (void *)block, NULL);
  return block;
  (void)self;
}

/**
Runs the required block after the specified number of milliseconds have passed.
Time is counted only once Iodine started running (using {Iodine.start}).

Tasks scheduled before calling {Iodine.start} will run once for every process.

Always returns a copy of the block object.
*/
static VALUE iodine_defer_run_after(VALUE self, VALUE milliseconds) {
  (void)(self);
  if (milliseconds == Qnil) {
    return iodine_defer_run(self);
  }
  if (TYPE(milliseconds) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "milliseconds must be a number");
    return Qnil;
  }
  size_t milli = FIX2UINT(milliseconds);
  if (milli == 0) {
    return iodine_defer_run(self);
  }
  // requires a block to be passed
  rb_need_block();
  VALUE block = rb_block_proc();
  if (block == Qnil)
    return Qfalse;
  IodineStore.add(block);
  if (facil_run_every(milli, 1, iodine_defer_run_timer, (void *)block,
                      (void (*)(void *))IodineStore.remove) == -1) {
    perror("ERROR: Iodine couldn't initialize timer");
    return Qnil;
  }
  return block;
}
/**
Runs the required block after the specified number of milliseconds have passed.
Time is counted only once Iodine started running (using {Iodine.start}).

Accepts:

milliseconds:: the number of milliseconds between event repetitions.

repetitions:: the number of event repetitions. Defaults to 0 (never ending).

block:: (required) a block is required, as otherwise there is nothing to
perform.

The event will repeat itself until the number of repetitions had been delpeted.

Always returns a copy of the block object.
*/
static VALUE iodine_defer_run_every(int argc, VALUE *argv, VALUE self) {
  (void)(self);
  VALUE milliseconds, repetitions, block;

  rb_scan_args(argc, argv, "11&", &milliseconds, &repetitions, &block);

  if (TYPE(milliseconds) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "milliseconds must be a number.");
    return Qnil;
  }
  if (repetitions != Qnil && TYPE(repetitions) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "repetitions must be a number or `nil`.");
    return Qnil;
  }

  size_t milli = FIX2UINT(milliseconds);
  size_t repeat = (repetitions == Qnil) ? 0 : FIX2UINT(repetitions);
  // requires a block to be passed
  rb_need_block();
  IodineStore.add(block);
  if (facil_run_every(milli, repeat, iodine_defer_run_timer, (void *)block,
                      (void (*)(void *))IodineStore.remove) == -1) {
    perror("ERROR: Iodine couldn't initialize timer");
    return Qnil;
  }
  return block;
}

/* *****************************************************************************
Pre/Post `fork`
***************************************************************************** */
#include "fio_llist.h"
#include "spnlock.inc"

static spn_lock_i iodine_before_fork_lock = SPN_LOCK_INIT;
static fio_ls_s iodine_before_fork_list = FIO_LS_INIT(iodine_before_fork_list);
static spn_lock_i iodine_after_fork_lock = SPN_LOCK_INIT;
static fio_ls_s iodine_after_fork_list = FIO_LS_INIT(iodine_after_fork_list);

/**
Sets a block of code to run before a new worker process is forked.
*/
VALUE iodine_before_fork_add(VALUE self) {
  rb_need_block();
  VALUE block = rb_block_proc();
  rb_global_variable(&block);
  spn_lock(&iodine_before_fork_lock);
  fio_ls_push(&iodine_before_fork_list, (void *)block);
  spn_unlock(&iodine_before_fork_lock);
  return block;
  (void)self;
}

/**
Sets a block of code to run after a new worker process is forked.
*/
VALUE iodine_after_fork_add(VALUE self) {
  rb_need_block();
  VALUE block = rb_block_proc();
  rb_global_variable(&block);
  spn_lock(&iodine_after_fork_lock);
  fio_ls_push(&iodine_after_fork_list, (void *)block);
  spn_unlock(&iodine_after_fork_lock);
  return block;
  (void)self;
}

/* Runs the before / after fork callbacks (if `before` is true, before runs) */
static void iodine_perform_fork_callbacks(uint8_t before) {
  fio_ls_s *ls = before ? &iodine_before_fork_list : &iodine_after_fork_list;
  spn_lock_i *lock =
      before ? &iodine_before_fork_lock : &iodine_after_fork_lock;
  spn_lock(lock);
  FIO_LS_FOR(ls, pos) { IodineCaller.call((VALUE)(pos->obj), call_id); }
  spn_unlock(lock);
}
/* *****************************************************************************
Add defer API to Iodine
***************************************************************************** */

void iodine_defer_initialize(void) {
  call_id = rb_intern2("call", 4);
  rb_define_module_function(IodineModule, "run", iodine_defer_run, 0);
  rb_define_module_function(IodineModule, "defer", iodine_defer_run, 0);

  rb_define_module_function(IodineModule, "run_after", iodine_defer_run_after,
                            1);
  rb_define_module_function(IodineModule, "run_every", iodine_defer_run_every,
                            -1);
  rb_define_module_function(IodineModule, "before_fork", iodine_before_fork_add,
                            0);
  rb_define_module_function(IodineModule, "after_fork", iodine_after_fork_add,
                            0);
  defer(iodine_start_io_thread, NULL, NULL);
}
