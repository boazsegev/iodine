#ifndef H___IODINE_DEFER___H
#define H___IODINE_DEFER___H
#include "iodine.h"

/* *****************************************************************************

                  Iodine's `defer` and `run_after` functions

***************************************************************************** */

/* *****************************************************************************
Iodine's `on_state`
***************************************************************************** */

static ID IODINE_STATE_PRE_START;
static ID IODINE_STATE_BEFORE_FORK;
static ID IODINE_STATE_AFTER_FORK;
static ID IODINE_STATE_ENTER_CHILD;
static ID IODINE_STATE_ENTER_MASTER;
static ID IODINE_STATE_ON_START;
static ID IODINE_STATE_ON_PARENT_CRUSH;
static ID IODINE_STATE_ON_CHILD_CRUSH;
static ID IODINE_STATE_ON_SHUTDOWN;
static ID IODINE_STATE_ON_STOP;

/* performs a Ruby state callback without clearing the Ruby object's memory */
static void iodine_perform_state_callback_persist(void *blk_) {
  VALUE blk = (VALUE)blk_;
  // iodine_caller_result_s r =
  iodine_ruby_call_outside(blk, IODINE_CALL_ID, 0, NULL);
}

// clang-format off
/**
Sets a block of code to run when Iodine's core state is updated.

@param [Symbol] event the state event for which the block should run (see list).
@since 0.7.9

The state event Symbol can be any of the following:

|  |  |
|---|---|
| `:pre_start`   | the block will be called once before starting up the IO reactor. |
| `:before_fork` | the block will be called before each time the IO reactor forks a new worker. |
| `:after_fork`  | the block will be called after each fork (both in parent and workers). |
| `:enter_child` | the block will be called by a worker process right after forking. |
| `:enter_master` | the block will be called by the master process after spawning a worker (after forking). |
| `:on_start`     | the block will be called every time a *worker* process starts. In single process mode, the master process is also a worker. |
| `:on_parent_crush` | the block will be called by each worker the moment it detects the master process crashed. |
| `:on_child_crush`  | the block will be called by the parent (master) after a worker process crashed. |
| `:start_shutdown`  | the block will be called before starting the shutdown sequence. |
| `:on_finish`       | the block will be called just before finishing up (both on chlid and parent processes). |

Code runs in both the parent and the child.
*/
static VALUE iodine_on_state(VALUE self, VALUE event) { // clang-format on

  rb_need_block();
  Check_Type(event, T_SYMBOL);
  VALUE block = rb_block_proc();
  STORE.hold(block);
  ID state = rb_sym2id(event);

  if (state == IODINE_STATE_PRE_START) {
    fio_state_callback_add(FIO_CALL_PRE_START,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_BEFORE_FORK) {
    fio_state_callback_add(FIO_CALL_BEFORE_FORK,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_AFTER_FORK) {
    fio_state_callback_add(FIO_CALL_AFTER_FORK,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_ENTER_CHILD) {
    fio_state_callback_add(FIO_CALL_IN_CHILD,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_ENTER_MASTER) {
    fio_state_callback_add(FIO_CALL_IN_MASTER,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_ON_START) {
    fio_state_callback_add(FIO_CALL_ON_START,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_ON_PARENT_CRUSH) {
    fio_state_callback_add(FIO_CALL_ON_PARENT_CRUSH,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_ON_CHILD_CRUSH) {
    fio_state_callback_add(FIO_CALL_ON_CHILD_CRUSH,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_ON_SHUTDOWN) {
    fio_state_callback_add(FIO_CALL_ON_SHUTDOWN,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else if (state == IODINE_STATE_ON_STOP) {
    fio_state_callback_add(FIO_CALL_ON_STOP,
                           iodine_perform_state_callback_persist,
                           (void *)block);
  } else {
    STORE.release(block);
    rb_raise(rb_eTypeError, "unknown event in Iodine.on_state");
  }
  return block;
  (void)self;
}

/* *****************************************************************************
Task performance
***************************************************************************** */

static void iodine_defer_performe_once(void *block, void *ignr) {
  iodine_ruby_call_outside((VALUE)block, IODINE_CALL_ID, 0, NULL);
  STORE.release((VALUE)block);
  (void)ignr;
}

static int iodine_defer_run_timer(void *block, void *ignr) {
  iodine_caller_result_s r =
      iodine_ruby_call_outside((VALUE)block, IODINE_CALL_ID, 0, NULL);
  return 0 - (r.exception || (r.result == Qfalse));
  (void)ignr;
}

static void iodine_defer_after_timer(void *block, void *ignr) {
  STORE.release((VALUE)block);
  (void)ignr;
}

/* *****************************************************************************
Defer API
***************************************************************************** */

/**
 * Runs a block of code synchronously (adds the code to the IO event queue).
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
  VALUE block = rb_block_proc();
  STORE.hold(block);
  fio_io_defer(iodine_defer_performe_once, (void *)block, NULL);
  return block;
  (void)self;
}

/**
 * Runs a block of code asynchronously (adds the code to the async event queue).
 *
 * Always returns the block of code to executed (Proc object).
 *
 * Code will be executed only while Iodine is running (after {Iodine.start}).
 *
 * Code blocks that where scheduled to run before Iodine enters cluster mode
 * will run on all child processes.
 */
static VALUE iodine_defer_run_async(VALUE self) {
  rb_need_block();
  VALUE block = rb_block_proc();
  STORE.hold(block);
  IODINE_DEFER_BLOCK(block);
  return block;
  (void)self;
}

// clang-format off
/**
Runs the required block after the specified number of milliseconds have passed.
Time is counted only once Iodine started running (using {Iodine.start}).

Accepts:

|   |   |
|---|---|
| `:milliseconds` | the number of milliseconds between event repetitions.|
| `:repetitions` | the number of event repetitions. Defaults to 1 (performed once). Set to 0 for never ending. |
| `:block` | (required) a block is required, as otherwise there is nothing to|
perform.

The event will repeat itself until the number of repetitions had been delpeted.

Always returns a copy of the block object.
*/
static VALUE iodine_defer_run_after(int argc, VALUE *argv, VALUE self) {
  // clang-format on
  (void)(self);
  int64_t milli = 0;
  int64_t repeat = 1;
  VALUE block = Qnil;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_NUM(milli, 0, "milliseconds", 1), // required
                  IODINE_ARG_NUM(repeat, 0, "repetitions", 0),
                  IODINE_ARG_PROC(block, 0, "block", 1));
  STORE.hold(block);
  repeat -= 1;
  fio_io_run_every(.every = (uint32_t)milli,
                   .repetitions = (int32_t)repeat,
                   .fn = iodine_defer_run_timer,
                   .udata1 = (void *)block,
                   .on_finish = iodine_defer_after_timer);
  return block;
}

#endif /* H___IODINE_DEFER___H */
