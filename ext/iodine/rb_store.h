#ifndef H_RB_STORE_H
#define H_RB_STORE_H value

#include "ruby.h"

extern struct IodineStorage_s {
  /** Adds an object to the storage (or increases it's reference count). */
  void (*add)(VALUE);
  /** Removes an object from the storage (or decreases it's reference count). */
  void (*remove)(VALUE);
  /** Should be called after forking to reset locks */
  void (*after_fork)(void);
  /** Prints debugging information to the console. */
  void (*print)(void);
} RBStore;

/** Initializes the storage unit for first use. */
void iodine_storage_init(void);

#endif
