#ifndef H_IODINE_STORAGE_H
#define H_IODINE_STORAGE_H

#include "ruby.h"

extern struct IodineStorage_s {
  /** Adds an object to the storage (or increases it's reference count). */
  VALUE (*add)(VALUE);
  /** Removes an object from the storage (or decreases it's reference count). */
  VALUE (*remove)(VALUE);
  /** Should be called after forking to reset locks */
  void (*after_fork)(void);
  /** Prints debugging information to the console. */
  void (*print)(void);
} IodineStore;

/** Initializes the storage unit for first use. */
void iodine_storage_init(void);

#endif
