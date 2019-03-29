#ifndef H_IODINE_CALLER_H
#define H_IODINE_CALLER_H

#include "ruby.h"

#include <stdint.h>

extern struct IodineCaller_s {
  /** Calls a C function within the GVL (unprotected). */
  void *(*enterGVL)(void *(*func)(void *), void *arg);
  /** Calls a C function outside the GVL (no Ruby API calls allowed). */
  void *(*leaveGVL)(void *(*func)(void *), void *arg);
  /** Calls a Ruby method on a given object, protecting against exceptions. */
  VALUE (*call)(VALUE obj, ID method);
  /** Calls a Ruby method on a given object, protecting against exceptions. */
  VALUE (*call2)(VALUE obj, ID method, int argc, VALUE *argv);
  /** Calls a Ruby method on a given object, protecting against exceptions. */
  VALUE(*call_with_block)
  (VALUE obj, ID method, int argc, VALUE *argv, VALUE udata,
   VALUE (*block_func)(VALUE block_argv1, VALUE udata, int argc, VALUE *argv));
  /** Returns the GVL state flag. */
  uint8_t (*in_GVL)(void);
  /** Forces the GVL state flag. */
  void (*set_GVL)(uint8_t state);
} IodineCaller;

#endif
