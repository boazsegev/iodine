#include "iodine.h"

#include "iodine_store.h"

#include <inttypes.h>
#include <stdint.h>

#define FIO_SET_NAME fio_store
#define FIO_SET_OBJ_TYPE uintptr_t
#include <fio.h>

static fio_lock_i iodine_storage_lock = FIO_LOCK_INIT;
static fio_store_s iodine_storage = FIO_SET_INIT;
static size_t iodine_storage_count_max = 0;

#ifndef IODINE_DEBUG
#define IODINE_DEBUG 0
#endif

/* *****************************************************************************
API
***************************************************************************** */

/** Adds an object to the storage (or increases it's reference count). */
static VALUE storage_add(VALUE obj) {
  if (!obj || obj == Qnil || obj == Qtrue || obj == Qfalse)
    return obj;
  uintptr_t old = 0;
  fio_lock(&iodine_storage_lock);
  fio_store_overwrite(&iodine_storage, obj, 1, &old);
  if (old)
    fio_store_overwrite(&iodine_storage, obj, old + 1, NULL);
  if (iodine_storage_count_max < fio_store_count(&iodine_storage))
    iodine_storage_count_max = fio_store_count(&iodine_storage);
  fio_unlock(&iodine_storage_lock);
  return obj;
}
/** Removes an object from the storage (or decreases it's reference count). */
static VALUE storage_remove(VALUE obj) {
  if (!obj || obj == Qnil || obj == Qtrue || obj == Qfalse ||
      iodine_storage.count == 0)
    return obj;
  fio_lock(&iodine_storage_lock);
  uintptr_t old = 0;
  fio_store_remove(&iodine_storage, obj, 0, &old);
  if (old > 1)
    fio_store_overwrite(&iodine_storage, obj, old - 1, NULL);
  fio_unlock(&iodine_storage_lock);
  return obj;
}
/** Should be called after forking to reset locks */
static void storage_after_fork(void) { iodine_storage_lock = FIO_LOCK_INIT; }

/** Prints debugging information to the console. */
static void storage_print(void) {
  FIO_LOG_DEBUG("Ruby <=> C Memory storage stats (pid: %d):\n", getpid());
  fio_lock(&iodine_storage_lock);
  uintptr_t index = 0;
  FIO_SET_FOR_LOOP(&iodine_storage, pos) {
    if (pos->obj) {
      fprintf(stderr, "[%" PRIuPTR "] => %" PRIuPTR " X obj %p type %d\n",
              index++, pos->obj, (void *)pos->hash, TYPE(pos->hash));
    }
  }
  fprintf(stderr, "Total of %" PRIuPTR " objects protected form GC\n", index);
  fprintf(stderr,
          "Storage uses %" PRIuPTR " Hash bins for %" PRIuPTR " objects\n"
          "The largest collection was %zu objects.\n",
          iodine_storage.capa, iodine_storage.count, iodine_storage_count_max);
  fio_unlock(&iodine_storage_lock);
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
  if (FIO_LOG_LEVEL >= FIO_LOG_LEVEL_DEBUG)
    storage_print();
  fio_lock(&iodine_storage_lock);
  // fio_store_compact(&iodine_storage);
  FIO_SET_FOR_LOOP(&iodine_storage, pos) {
    if (pos->obj) {
      rb_gc_mark((VALUE)pos->hash);
    }
  }
  fio_unlock(&iodine_storage_lock);
}

/* clear the registry (end of lifetime) */
static void storage_clear(void *ignore) {
  (void)ignore;
  FIO_LOG_DEBUG("Ruby<=>C Storage cleared.\n");
  fio_lock(&iodine_storage_lock);
  fio_store_free(&iodine_storage);
  iodine_storage = (fio_store_s)FIO_SET_INIT;
  fio_unlock(&iodine_storage_lock);
}

/*
the data-type used to identify the registry
this sets the callbacks.
*/
static const struct rb_data_type_struct storage_type_struct = {
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
  fio_store_capa_require(&iodine_storage, 512);
  VALUE tmp =
      rb_define_class_under(rb_cObject, "IodineObjectStorage", rb_cData);
  VALUE storage_obj =
      TypedData_Wrap_Struct(tmp, &storage_type_struct, &iodine_storage);
  // rb_global_variable(&iodine_storage_obj);
  rb_ivar_set(IodineModule, rb_intern2("storage", 7), storage_obj);
  rb_define_module_function(IodineBaseModule, "db_print_protected_objects",
                            storage_print_rb, 0);
}
