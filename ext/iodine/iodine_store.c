#include "iodine.h"

#include "fio_hashmap.h"
#include "iodine_store.h"

#include <fio.h>
#include <inttypes.h>
#include <stdint.h>

fio_lock_i lock = FIO_LOCK_INIT;
fio_hash_s storage = FIO_HASH_INIT;

#ifndef IODINE_DEBUG
#define IODINE_DEBUG 0
#endif

/* *****************************************************************************
API
***************************************************************************** */

/** Adds an object to the storage (or increases it's reference count). */
static VALUE storage_add(VALUE obj) {
  if (obj == Qnil || obj == Qtrue || obj == Qfalse)
    return obj;
  fio_lock(&lock);
  uintptr_t val = (uintptr_t)fio_hash_insert(&storage, obj, (void *)1);
  if (val) {
    fio_hash_insert(&storage, obj, (void *)(val + 1));
  }
  fio_unlock(&lock);
  return obj;
}
/** Removes an object from the storage (or decreases it's reference count). */
static VALUE storage_remove(VALUE obj) {
  if (obj == Qnil || obj == Qtrue || obj == Qfalse || storage.map == NULL ||
      storage.count == 0)
    return obj;
  fio_lock(&lock);
  uintptr_t val = (uintptr_t)fio_hash_insert(&storage, obj, NULL);
  if (val > 1) {
    fio_hash_insert(&storage, obj, (void *)(val - 1));
  }
  if ((storage.count << 1) <= storage.pos &&
      (storage.pos << 1) > storage.capa) {
    fio_hash_compact(&storage);
  }
  fio_unlock(&lock);
  return obj;
}
/** Should be called after forking to reset locks */
static void storage_after_fork(void) { lock = FIO_LOCK_INIT; }

/** Prints debugging information to the console. */
static void storage_print(void) {
  fprintf(stderr, "Ruby <=> C Memory storage stats (pid: %d):\n", getpid());
  fio_lock(&lock);
  uintptr_t index = 0;
  FIO_HASH_FOR_LOOP(&storage, pos) {
    if (pos->obj) {
      fprintf(stderr, "[%" PRIuPTR "] => %" PRIuPTR " X obj %p type %d\n",
              index++, (uintptr_t)pos->obj, (void *)pos->key, TYPE(pos->key));
    }
  }
  fprintf(stderr, "Total of %" PRIuPTR " objects protected form GC\n", index);
  fprintf(stderr,
          "Storage uses %" PRIuPTR " Hash bins for %" PRIuPTR " objects\n",
          storage.capa, storage.count);
  fio_unlock(&lock);
}

/**
 * Used for debugging purposes (when testing iodine for Ruby object "leaks").
 */
static VALUE storage_print_rb(VALUE self) {
  storage_print();
  return Qnil;
  (void)self;
}
/* *****************************************************************************
GC protection
***************************************************************************** */

/* a callback for the GC (marking active objects) */
static void storage_mark(void *ignore) {
  (void)ignore;
#if IODINE_DEBUG
  storage_print();
#endif
  fio_lock(&lock);
  // fio_hash_compact(&storage);
  FIO_HASH_FOR_LOOP(&storage, pos) {
    if (pos->obj) {
      rb_gc_mark((VALUE)pos->key);
    }
  }
  fio_unlock(&lock);
}

/* clear the registry (end of lifetime) */
static void storage_clear(void *ignore) {
  (void)ignore;
#if IODINE_DEBUG == 1
  fprintf(stderr, "* INFO: Ruby<=>C Storage cleared.\n");
#endif
  fio_lock(&lock);
  fio_hash_free(&storage);
  storage = (fio_hash_s)FIO_HASH_INIT;
  fio_unlock(&lock);
}

/*
the data-type used to identify the registry
this sets the callbacks.
*/
static struct rb_data_type_struct storage_type_struct = {
    .wrap_struct_name = "RubyReferencesIn_C_Land",
    .function.dfree = (void (*)(void *))storage_clear,
    .function.dmark = (void (*)(void *))storage_mark,
};

/* *****************************************************************************
Initialization
***************************************************************************** */

struct IodineStorage_s IodineStore = {
    .add = storage_add,
    .remove = storage_remove,
    .after_fork = storage_after_fork,
    .print = storage_print,
};

/** Initializes the storage unit for first use. */
void iodine_storage_init(void) {
  fio_hash_new2(&storage, 512);
  VALUE tmp =
      rb_define_class_under(rb_cObject, "IodineObjectStorage", rb_cData);
  VALUE storage_obj =
      TypedData_Wrap_Struct(tmp, &storage_type_struct, &storage);
  // rb_global_variable(&storage_obj);
  rb_ivar_set(IodineModule, rb_intern2("storage", 7), storage_obj);
  rb_define_module_function(IodineBaseModule, "db_print_protected_objects",
                            storage_print_rb, 0);
}
