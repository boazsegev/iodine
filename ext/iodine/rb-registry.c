/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "rb-registry.h"
#include <ruby.h>

#include "spnlock.inc"

#define FIO_OVERRIDE_MALLOC 1
#include "fio_mem.h"

#include "fio_hashmap.h"
#include <signal.h>

#ifndef RUBY_REG_DBG
#define RUBY_REG_DBG 0
#endif

// the registry state keeper
static struct {
  fio_hash_s store;
  VALUE owner;
  spn_lock_i lock;
} registry = {.store = {.capa = 0}, .owner = 0, .lock = SPN_LOCK_INIT};

#define try_lock_registry() spn_trylock(&registry.lock)
#define unlock_registry() spn_unlock(&registry.lock)
#define lock_registry() spn_lock(&registry.lock)

/** adds an object to the registry or increases it's reference count. */
static VALUE register_object(VALUE ruby_obj) {
  if (!ruby_obj || ruby_obj == Qnil || ruby_obj == Qfalse)
    return 0;
  lock_registry();
  uintptr_t count = (uintptr_t)fio_hash_find(&registry.store, ruby_obj);
#if RUBY_REG_DBG == 1
  fprintf(stderr, "Ruby Registry: register %p ref: %" PRIu64 " + 1\n",
          (void *)ruby_obj, (uint64_t)count);
#endif
  fio_hash_insert(&registry.store, (uint64_t)ruby_obj, (void *)(count + 1));
  unlock_registry();
  return ruby_obj;
}

/** decreases an object's reference count or removes if from the registry. */
static void unregister_object(VALUE ruby_obj) {
  if (!ruby_obj || ruby_obj == Qnil)
    return;
  lock_registry();
  uintptr_t count = (uintptr_t)fio_hash_find(&registry.store, ruby_obj);
#if RUBY_REG_DBG == 1
  fprintf(stderr, "Ruby Registry: unregister %p ref: %" PRIu64 " - 1\n",
          (void *)ruby_obj, (uint64_t)count);
#endif
  if (count) {
    fio_hash_insert(&registry.store, (uint64_t)ruby_obj, (void *)(count - 1));
  }
  unlock_registry();
}

/* a callback for the GC (marking active objects) */
static void registry_mark(void *ignore) {
  (void)ignore;
#if RUBY_REG_DBG == 1
  Registry.print();
#endif
  lock_registry();
  fio_hash_compact(&registry.store);
  FIO_HASH_FOR_LOOP(&registry.store, pos) {
    if (pos->obj) {
      rb_gc_mark((VALUE)pos->key);
    }
  }
  unlock_registry();
}

/* clear the registry (end of lifetime) */
static void registry_clear(void *ignore) {
  (void)ignore;
#if RUBY_REG_DBG == 1
  fprintf(stderr, "Ruby Registry:  Clear!!!\n");
#endif
  lock_registry();
  fio_hash_free(&registry.store);
  registry.owner = 0;
  registry.store = (fio_hash_s){.capa = 0};
  unlock_registry();
}

/*
the data-type used to identify the registry
this sets the callbacks.
*/
static struct rb_data_type_struct my_registry_type_struct = {
    .wrap_struct_name = "RubyReferencesIn_C_Land",
    .function.dfree = (void (*)(void *))registry_clear,
    .function.dmark = (void (*)(void *))registry_mark,
};

/* initialize the registry */
static void init(VALUE owner) {
  lock_registry();
  // only one registry
  if (registry.owner)
    goto finish;
  if (!owner)
    owner = rb_cObject;
  registry.owner = owner;
  VALUE rReferences =
      rb_define_class_under(owner, "RubyObjectRegistry_for_C_land", rb_cData);
  VALUE r_registry =
      TypedData_Wrap_Struct(rReferences, &my_registry_type_struct, &registry);
  rb_ivar_set(owner, rb_intern("registry"), r_registry);
finish:
  unlock_registry();
}

/* print data, for testing */
static void print(void) {
  lock_registry();
  fprintf(stderr, "Registry owner is %lu\n", registry.owner);
  uintptr_t index = 0;
  FIO_HASH_FOR_LOOP(&registry.store, pos) {
    if (pos->obj) {
      fprintf(stderr, "[%" PRIuPTR " ] => %" PRIuPTR " X obj %p type %d\n",
              index++, (uintptr_t)pos->obj, (void *)pos->key, TYPE(pos->key));
    }
  }
  fprintf(stderr, "Total of %" PRIuPTR " registered objects being marked\n",
          index);
  fprintf(stderr,
          "Registry uses %" PRIuPTR " Hash bins for %" PRIuPTR " objects\n",
          registry.store.capa, registry.store.count);
  unlock_registry();
}

////////////////////////////////////////////
// The API gateway
struct ___RegistryClass___ Registry = {
    .init = init,
    .remove = unregister_object,
    .add = register_object,
    .print = print,
};
