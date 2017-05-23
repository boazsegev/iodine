/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "rb-registry.h"
#include <ruby.h>

#include "spnlock.inc"

// #define RUBY_REG_DBG

#define REGISTRY_POOL_SIZE 1024
// the references struct (bin-tree)
struct Object {
  struct Object *next;
  VALUE obj;
  int count;
};

// the registry global
static struct Registry {
  struct Object pool_mem[REGISTRY_POOL_SIZE];
  struct Object *obj_pool;
  struct Object *first;
  VALUE owner;
  spn_lock_i lock;
} registry = {
    .obj_pool = NULL, .first = NULL, .owner = 0, .lock = SPN_LOCK_INIT};

#define try_lock_registry() spn_trylock(&registry.lock)
#define unlock_registry() spn_unlock(&registry.lock)
#define lock_registry() spn_lock(&registry.lock)

inline static void free_node(struct Object *to_free) {
  if (to_free >= registry.pool_mem &&
      (intptr_t)to_free <= (intptr_t)(&registry.obj_pool)) {
    to_free->next = registry.obj_pool;
    registry.obj_pool = to_free;
  } else {
    free(to_free);
  }
}

// add an object to the registry
//
// allow multiple registrartions (bag)
static VALUE register_object(VALUE obj) {
  if (!obj || obj == Qnil)
    return 0;
  struct Object *line = registry.first;
  lock_registry();
  while (line) {
    if (line->obj == obj) {
      line->count++;
      goto finish;
    }
    line = line->next;
  }
  if (registry.obj_pool) {
    line = registry.obj_pool;
    registry.obj_pool = registry.obj_pool->next;
  } else {
    line = malloc(sizeof(struct Object));
  }
  if (line == NULL) {
    perror("No Memory!");
    exit(1);
  }
  line->obj = obj;
  line->next = registry.first;
  line->count = 1;
  registry.first = line;
finish:
  unlock_registry();
  return obj;
}

// free a single registry
//
// free only one.
static void unregister_object(VALUE obj) {
  if (!obj || obj == Qnil)
    return;
  lock_registry();
  struct Object **line = &registry.first;
  while (*line) {
    if ((*line)->obj == obj) {
      (*line)->count -= 1;
      if ((*line)->count <= 0) {
        struct Object *to_free = *line;
        *line = (*line)->next;
        free_node(to_free);
        goto finish;
      }
    }
    line = &((*line)->next);
  }
finish:
  unlock_registry();
}

// // Replaces one registry object with another,
// // allowing updates to the Registry with no memory allocations.
// //
// // returns 0 if all OK, returns -1 if it couldn't replace the object.
// static int replace_object(VALUE obj, VALUE new_obj) {
//   int ret = -1;
//   if (obj == new_obj)
//     return 0;
//   pthread_mutex_lock(&registry_lock);
//   struct Object* line = registry.first;
//   while (line) {
//     if (line->obj == obj) {
//       line->obj = new_obj;
//       ret = 0;
//       goto finish;
//     }
//     line = line->next;
//   }
// finish:
//   pthread_mutex_unlock(&registry_lock);
//   return ret;
// }

// a callback for the GC (marking active objects)
static void registry_mark(void *ignore) {
  (void)ignore;
#ifdef RUBY_REG_DBG
  Registry.print();
#endif
  lock_registry();
  struct Object *line = registry.first;
  while (line) {
    if (line->obj)
      rb_gc_mark(line->obj);
    line = line->next;
  }
  unlock_registry();
}

// clear the registry (end of lifetime)
static void registry_clear(void *ignore) {
  (void)ignore;
  lock_registry();
  struct Object *line;
  struct Object *to_free;
  // free active object references
  line = registry.first;
  while (line) {
    to_free = line;
    line = line->next;
    free_node(to_free);
  }
  registry.first = NULL;
  registry.owner = 0;
  unlock_registry();
}

// the data-type used to identify the registry
// this sets the callbacks.
static struct rb_data_type_struct my_registry_type_struct = {
    .wrap_struct_name = "RubyReferencesIn_C_Land",
    .function.dfree = (void (*)(void *))registry_clear,
    .function.dmark = (void (*)(void *))registry_mark,
};

// initialize the registry
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
  for (size_t i = 0; i < REGISTRY_POOL_SIZE - 1; i++) {
    registry.pool_mem[i].next = registry.pool_mem + i + 1;
  }
  registry.pool_mem[REGISTRY_POOL_SIZE - 1].next = NULL;
  registry.obj_pool = registry.pool_mem;
finish:
  unlock_registry();
}

// print data, for testing
static void print(void) {
  lock_registry();
  struct Object *line = registry.first;
  fprintf(stderr, "Registry owner is %lu\n", registry.owner);
  long index = 0;
  while (line) {
    fprintf(stderr, "[%lu] => %d X obj %lu type %d at %p\n", index++,
            line->count, line->obj, TYPE(line->obj), (void *)line);
    line = line->next;
  }
  fprintf(stderr, "Total of %lu registered objects being marked\n", index);
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
