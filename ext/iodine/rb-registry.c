/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "rb-registry.h"
#include <ruby.h>

#include "spnlock.inc"

#include "fio_hash_table.h"
#include <signal.h>

// #define RUBY_REG_DBG
#ifndef REGISTRY_POOL_SIZE
#define REGISTRY_POOL_SIZE 1024
#endif

#ifndef RUBY_REG_DBG
#define RUBY_REG_DBG 0
#endif

typedef struct {
  union {
    fio_list_s pool;
    fio_ht_node_s node;
  };
  VALUE obj;
  volatile uint64_t ref;
} obj_s;

// the registry state keeper
static struct {
  obj_s pool_mem[REGISTRY_POOL_SIZE];
  fio_list_s pool;
  fio_ht_s store;
  VALUE owner;
  spn_lock_i lock;
} registry = {.pool = FIO_LIST_INIT_STATIC(registry.pool),
              .store = FIO_HASH_TABLE_STATIC(registry.store),
              .owner = 0,
              .lock = SPN_LOCK_INIT};

#define try_lock_registry() spn_trylock(&registry.lock)
#define unlock_registry() spn_unlock(&registry.lock)
#define lock_registry() spn_lock(&registry.lock)

inline static void free_node(obj_s *to_free) {
  if (to_free >= registry.pool_mem &&
      (intptr_t)to_free <= (intptr_t)(&registry.pool))
    fio_list_push(obj_s, pool, registry.pool, to_free);
  else
    free(to_free);
}

/** adds an object to the registry or increases it's reference count. */
static VALUE register_object(VALUE ruby_obj) {
  if (!ruby_obj || ruby_obj == Qnil || ruby_obj == Qfalse)
    return 0;
  lock_registry();
  obj_s *obj = (void *)fio_ht_find(&registry.store, (uint64_t)ruby_obj);
  if (obj) {
    obj = fio_node2obj(obj_s, node, obj);
#if RUBY_REG_DBG == 1
    fprintf(stderr, "Ruby Registry: register %p ref: %" PRIu64 " + 1\n",
            (void *)ruby_obj, obj->ref);
#endif
    goto exists;
  }
#if RUBY_REG_DBG == 1
  fprintf(stderr, "Ruby Registry: register %p\n", (void *)ruby_obj);
#endif
  obj = fio_list_pop(obj_s, pool, registry.pool);
  if (!obj)
    obj = malloc(sizeof(obj_s));
  if (!obj) {
    perror("No Memory!");
    kill(0, SIGINT);
    exit(1);
  }
  *obj = (obj_s){.obj = ruby_obj};
  fio_ht_add(&registry.store, &obj->node, (uint64_t)ruby_obj);
exists:
  spn_add(&obj->ref, 1);

  unlock_registry();
  return ruby_obj;
}

/** decreases an object's reference count or removes if from the registry. */
static void unregister_object(VALUE ruby_obj) {
  if (!ruby_obj || ruby_obj == Qnil)
    return;
  lock_registry();
  obj_s *obj = (void *)fio_ht_find(&registry.store, (uint64_t)ruby_obj);
  if (!obj) {
#if RUBY_REG_DBG == 1
    fprintf(stderr, "Ruby Registry: unregister - NOT FOUND %p\n",
            (void *)ruby_obj);
#endif
    goto finish;
  }
  obj = fio_node2obj(obj_s, node, obj);
  if (spn_sub(&obj->ref, 1)) {
    unlock_registry();
#if RUBY_REG_DBG == 1
    fprintf(stderr, "Ruby Registry: unregistered %p ref: %" PRIu64 "  \n",
            (void *)ruby_obj, obj->ref);
#endif
    return;
  }
  fio_ht_remove(&obj->node);
  free_node(obj);
finish:
  unlock_registry();
#if RUBY_REG_DBG == 1
  fprintf(stderr, "Ruby Registry: unregistered %p\n", (void *)ruby_obj);
#endif
}

/* a callback for the GC (marking active objects) */
static void registry_mark(void *ignore) {
  (void)ignore;
#if RUBY_REG_DBG == 1
  Registry.print();
#endif
  lock_registry();
  obj_s *obj;
  fio_ht_for_each(obj_s, node, obj, registry.store) rb_gc_mark(obj->obj);
  unlock_registry();
}

/* clear the registry (end of lifetime) */
static void registry_clear(void *ignore) {
  (void)ignore;
#if RUBY_REG_DBG == 1
  fprintf(stderr, "Ruby Registry:  Clear!!!\n");
#endif
  lock_registry();
  obj_s *obj;
  fio_ht_for_each(obj_s, node, obj, registry.store) {
    fio_ht_remove(&obj->node);
    rb_gc_mark(obj->obj);
  }
  registry.owner = 0;
  fio_ht_free(&registry.store);
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
  // initialize memory pool
  for (size_t i = 0; i < REGISTRY_POOL_SIZE; i++) {
    fio_list_push(obj_s, pool, registry.pool, &registry.pool_mem[i]);
  }
finish:
  unlock_registry();
}

/* print data, for testing */
static void print(void) {
  lock_registry();
  fprintf(stderr, "Registry owner is %lu\n", registry.owner);
  obj_s *obj;
  uint64_t index = 0;
  fio_ht_for_each(obj_s, node, obj, registry.store) {
    fprintf(stderr, "[%" PRIu64 " ] => %" PRIu64 " X obj %p type %d at %p\n",
            index++, obj->ref, (void *)obj->obj, TYPE(obj->obj), (void *)obj);
  }
  fprintf(stderr, "Total of %" PRIu64 " registered objects being marked\n",
          index);
  fprintf(stderr,
          "Registry uses %" PRIu64 " Hash bins for %" PRIu64 " objects\n",
          registry.store.bin_count, registry.store.count);
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
