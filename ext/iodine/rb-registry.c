#include "rb-registry.h"
#include <ruby.h>
#include <pthread.h>
#include <stdatomic.h>

// #define RUBY_REG_DBG

// the registry global
static struct Registry {
  struct Object* obj_pool;
  struct Object* first;
  VALUE owner;
  atomic_bool lock;
} registry = {.obj_pool = NULL, .first = NULL, .owner = 0};

#define try_lock_registry() atomic_exchange(&registry.lock, 1)
#define unlock_registry() atomic_store(&registry.lock, 0)
#define lock_registry()         \
  {                             \
    while (try_lock_registry()) \
      sched_yield();            \
  }

// the references struct (bin-tree)
struct Object {
  struct Object* next;
  VALUE obj;
  int count;
};

// manage existing objects - add a reference
int add_reference(VALUE obj) {
  struct Object* line;
  lock_registry();
  line = registry.first;
  while (line) {
    if (line->obj == obj) {
      line->count++;
      unlock_registry();
      return 1;
    }
    line = line->next;
  }
  unlock_registry();
  return 0;
}

// add an object to the registry
//
// allow multiple registrartions (bag)
static VALUE register_object(VALUE obj) {
  if (!obj || obj == Qnil)
    return 0;
  if (add_reference(obj))
    return obj;
  struct Object* line;
  lock_registry();
  if (registry.obj_pool) {
    line = registry.obj_pool;
    registry.obj_pool = registry.obj_pool->next;
  } else {
    line = malloc(sizeof(struct Object));
  }
  if (!line) {
    perror("No Memory");
    unlock_registry();
    return 0;
  }
  line->obj = obj;
  line->next = registry.first;
  line->count = 1;
  registry.first = line;
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
  struct Object* line = registry.first;
  struct Object* prev = NULL;
  while (line) {
    if (line->obj == obj) {
      line->count--;
      if (!line->count) {
        if (line == registry.first)
          registry.first = line->next;
        else if (prev)  // must be true, really
          prev->next = line->next;
        // move the object container to the discarded object pool
        line->next = registry.obj_pool;
        registry.obj_pool = line;
      }
      goto finish;
    }
    prev = line;
    line = line->next;
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
static void registry_mark(void* ignore) {
#ifdef RUBY_REG_DBG
  Registry.print();
#endif
  lock_registry();
  struct Object* line = registry.first;
  while (line) {
    if (line->obj)
      rb_gc_mark(line->obj);
    line = line->next;
  }
  unlock_registry();
}

// clear the registry (end of lifetime)
static void registry_clear(void* ignore) {
  lock_registry();
  struct Object* line;
  struct Object* to_free;
  // free active object references
  line = registry.first;
  while (line) {
    to_free = line;
    line = line->next;
    free(to_free);
  }
  registry.first = NULL;
  // free container pool
  line = registry.obj_pool;
  while (line) {
    to_free = line;
    line = line->next;
    free(to_free);
  }
  registry.obj_pool = NULL;
  registry.owner = 0;
  unlock_registry();
}

// the data-type used to identify the registry
// this sets the callbacks.
static struct rb_data_type_struct my_registry_type_struct = {
    .wrap_struct_name = "RubyReferencesIn_C_Land",
    .function.dfree = (void (*)(void*))registry_clear,
    .function.dmark = (void (*)(void*))registry_mark,
};

// initialize the registry
static void init(VALUE owner) {
  lock_registry();
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

// print data, for testing
static void print(void) {
  lock_registry();
  struct Object* line = registry.first;
  fprintf(stderr, "Registry owner is %lu\n", registry.owner);
  long index = 0;
  while (line) {
    fprintf(stderr, "[%lu] => %d X obj %lu type %d at %p\n", index++,
            line->count, line->obj, TYPE(line->obj), line);
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
