#include "iodine.h"

#include <ruby/thread.h>

#include <stdint.h>
// clang-format on

#define FIO_INCLUDE_LINKED_LIST
#include "fio.h"

#include <pthread.h>

static ID STATE_PRE_START;
static ID STATE_BEFORE_FORK;
static ID STATE_AFTER_FORK;
static ID STATE_ENTER_CHILD;
static ID STATE_ENTER_MASTER;
static ID STATE_ON_START;
static ID STATE_ON_PARENT_CRUSH;
static ID STATE_ON_CHILD_CRUSH;
static ID STATE_START_SHUTDOWN;
static ID STATE_ON_FINISH;

/* *****************************************************************************
IO flushing dedicated thread for protection against blocking code
***************************************************************************** */

static fio_lock_i sock_io_thread_flag = 0;
static pthread_t sock_io_pthread;
typedef struct {
  size_t threads;
  size_t processes;
} iodine_start_settings_s;

static void *iodine_io_thread(void *arg) {
  (void)arg;
  while (sock_io_thread_flag) {
    if (fio_flush_all())
      fio_throttle_thread(500000UL);
    else
      fio_throttle_thread(150000000UL);
  }
  return NULL;
}
static void iodine_start_io_thread(void *a_) {
  if (!fio_trylock(&sock_io_thread_flag)) {
    if (pthread_create(&sock_io_pthread, NULL, iodine_io_thread, NULL)) {
      FIO_LOG_ERROR("Couldn't spawn IO thread.");
    };
    FIO_LOG_DEBUG("IO thread started.");
  }
  (void)a_;
}

static void iodine_join_io_thread(void) {
  if (fio_unlock(&sock_io_thread_flag) && sock_io_pthread) {
    pthread_join(sock_io_pthread, NULL);
    sock_io_pthread = (pthread_t)NULL;
    FIO_LOG_DEBUG("IO thread stopped and joined.");
  }
}

/* *****************************************************************************
The Defer library overriding functions
***************************************************************************** */

/* used to create Ruby threads and pass them the information they need */
struct CreateThreadArgs {
  void *(*thread_func)(void *);
  void *arg;
  fio_lock_i lock;
};

/* used for GVL signalling */
static void call_async_signal(void *pool) {
  fio_stop();
  (void)pool;
}

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
  fio_unlock(&old_args->lock);
  rb_thread_call_without_gvl(defer_thread_start, &args,
                             (void (*)(void *))call_async_signal, args.arg);
  return Qnil;
}

/* Within the GVL, creates a Ruby thread using an API call */
static void *create_ruby_thread_gvl(void *args) {
  return (void *)IodineStore.add(rb_thread_create(defer_thread_inGVL, args));
}

static void *fork_using_ruby(void *ignr) {
  // stop IO thread, if running (shouldn't occur)
  if (sock_io_pthread) {
    iodine_join_io_thread();
  }
  // fork using Ruby
  const VALUE ProcessClass = rb_const_get(rb_cObject, rb_intern2("Process", 7));
  const VALUE rb_pid = IodineCaller.call(ProcessClass, rb_intern2("fork", 4));
  intptr_t pid = 0;
  if (rb_pid != Qnil) {
    pid = NUM2INT(rb_pid);
  } else {
    pid = 0;
  }
  // manage post forking state for Iodine
  IodineCaller.set_GVL(1); /* enforce GVL state in thread storage */
  if (!pid) {
    IodineStore.after_fork();
  }
  return (void *)pid;
  (void)ignr;
}

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
void *fio_thread_new(void *(*thread_func)(void *), void *arg) {
  struct CreateThreadArgs data = (struct CreateThreadArgs){
      .thread_func = thread_func,
      .arg = arg,
      .lock = FIO_LOCK_INIT,
  };
  fio_lock(&data.lock);
  void *thr = IodineCaller.enterGVL(create_ruby_thread_gvl, &data);
  if (!thr || thr == (void *)Qnil || thr == (void *)Qfalse) {
    thr = NULL;
  } else {
    /* wait for thread to signal it's alive. */
    fio_lock(&data.lock);
  }
  return thr;
}

/**
OVERRIDE THIS to replace the default pthread implementation.
*/
int fio_thread_join(void *thr) {
  if (!thr || (VALUE)thr == Qfalse || (VALUE)thr == Qnil)
    return -1;
  IodineCaller.call((VALUE)thr, rb_intern("join"));
  IodineStore.remove((VALUE)thr);
  return 0;
}

// void defer_free_thread(void *thr) { (void)thr; }
void fio_thread_free(void *thr) { IodineStore.remove((VALUE)thr); }

/**
OVERRIDE THIS to replace the default `fork` implementation or to inject hooks
into the forking function.

Behaves like the system's `fork`.
*/
int fio_fork(void) {
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
  /* TODO, update return value to allow timer cancellation */
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
  fio_defer(iodine_defer_performe_once, (void *)block, NULL);
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
  if (fio_run_every(milli, 1, iodine_defer_run_timer, (void *)block,
                    (void (*)(void *))IodineStore.remove) == -1) {
    perror("ERROR: Iodine couldn't initialize timer");
    return Qnil;
  }
  return block;
}

// clang-format off
/**
Runs the required block after the specified number of milliseconds have passed.
Time is counted only once Iodine started running (using {Iodine.start}).

Accepts:

|   |   |
|---|---|
| `:milliseconds` | the number of milliseconds between event repetitions.|
| `:repetitions` | the number of event repetitions. Defaults to 0 (never ending).|
| `:block` | (required) a block is required, as otherwise there is nothing to|
perform.

The event will repeat itself until the number of repetitions had been delpeted.

Always returns a copy of the block object.
*/
static VALUE iodine_defer_run_every(int argc, VALUE *argv, VALUE self) {
  // clang-format on
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
  if (fio_run_every(milli, repeat, iodine_defer_run_timer, (void *)block,
                    (void (*)(void *))IodineStore.remove) == -1) {
    perror("ERROR: Iodine couldn't initialize timer");
    return Qnil;
  }
  return block;
}

/* *****************************************************************************
Pre/Post `fork`
***************************************************************************** */

/* performs a Ruby state callback without clearing the Ruby object's memory */
static void iodine_perform_state_callback_persist(void *blk_) {
  VALUE blk = (VALUE)blk_;
  IodineCaller.call(blk, call_id);
}

// clang-format off
/**
Sets a block of code to run when Iodine's core state is updated.

@param [Symbol] event the state event for which the block should run (see list).
@since 0.7.9

The state event Symbol can be any of the following:

|  |  |
|---|---|
| `:pre_start` | the block will be called once before starting up the IO reactor. |
| `:before_fork` | the block will be called before each time the IO reactor forks a new worker. |
| `:after_fork` | the block will be called after each fork (both in parent and workers). |
| `:enter_child` | the block will be called by a worker process right after forking. |
| `:enter_master` | the block will be called by the master process after spawning a worker (after forking). |
| `:on_start` | the block will be called every time a *worker* proceess starts. In single process mode, the master process is also a worker. |
| `:on_parent_crush` | the block will be called by each worker the moment it detects the master process crashed. |
| `:on_child_crush` | the block will be called by the parent (master) after a worker process crashed. |
| `:start_shutdown` | the block will be called before starting the shutdown sequence. |
| `:on_finish` | the block will be called just before finishing up (both on chlid and parent processes). |

Code runs in both the parent and the child.
*/
static VALUE iodine_on_state(VALUE self, VALUE event) {
  // clang-format on
  rb_need_block();
  Check_Type(event, T_SYMBOL);
  VALUE block = rb_block_proc();
  IodineStore.add(block);
  ID state = rb_sym2id(event);

  if (state == STATE_PRE_START) {
    fio_state_callback_add(FIO_CALL_PRE_START,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_BEFORE_FORK) {
    fio_state_callback_add(FIO_CALL_BEFORE_FORK,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_AFTER_FORK) {
    fio_state_callback_add(FIO_CALL_AFTER_FORK,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_ENTER_CHILD) {
    fio_state_callback_add(FIO_CALL_IN_CHILD,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_ENTER_MASTER) {
    fio_state_callback_add(FIO_CALL_IN_MASTER,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_ON_START) {
    fio_state_callback_add(FIO_CALL_ON_START,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_ON_PARENT_CRUSH) {
    fio_state_callback_add(FIO_CALL_ON_PARENT_CRUSH,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_ON_CHILD_CRUSH) {
    fio_state_callback_add(FIO_CALL_ON_CHILD_CRUSH,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_START_SHUTDOWN) {
    fio_state_callback_add(FIO_CALL_ON_SHUTDOWN,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == STATE_ON_FINISH) {
    fio_state_callback_add(FIO_CALL_ON_FINISH,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else {
    IodineStore.remove(block);
    rb_raise(rb_eTypeError, "unknown event in Iodine.on_state");
  }
  return block;
  (void)self;
}

/* Performs any cleanup before worker dies */
static void iodine_defer_on_finish(void *ignr) {
  (void)ignr;
  iodine_join_io_thread();
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
  rb_define_module_function(IodineModule, "on_state", iodine_on_state, 1);

  STATE_PRE_START = rb_intern("pre_start");
  STATE_BEFORE_FORK = rb_intern("before_fork");
  STATE_AFTER_FORK = rb_intern("after_fork");
  STATE_ENTER_CHILD = rb_intern("enter_child");
  STATE_ENTER_MASTER = rb_intern("enter_master");
  STATE_ON_START = rb_intern("on_start");
  STATE_ON_PARENT_CRUSH = rb_intern("on_parent_crush");
  STATE_ON_CHILD_CRUSH = rb_intern("on_child_crush");
  STATE_START_SHUTDOWN = rb_intern("start_shutdown");
  STATE_ON_FINISH = rb_intern("on_finish");

  /* start the IO thread is workrs (only starts in root if root is worker) */
  fio_state_callback_add(FIO_CALL_ON_START, iodine_start_io_thread, NULL);
  /* stop the IO thread before exit */
  fio_state_callback_add(FIO_CALL_ON_FINISH, iodine_defer_on_finish, NULL);
  /* kill IO thread even after a non-graceful iodine shutdown (force-quit) */
  fio_state_callback_add(FIO_CALL_AT_EXIT, iodine_defer_on_finish, NULL);
}
