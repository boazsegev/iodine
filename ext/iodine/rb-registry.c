#include "rb-registry.h"
#include <ruby.h>
#include <pthread.h>

// a mutex to protect the registry
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;

// the registry global
static struct Registry {
  struct Object* first;
  VALUE owner;
} registry = {.first = NULL, .owner = 0};
// the references struct (bin-tree)
struct Object {
  struct Object* next;
  VALUE obj;
};

// add an object to the registry
//
// allow multiple registrartions (bag)
static VALUE register_object(VALUE obj) {
  struct Object* line = malloc(sizeof(struct Object));
  if (!line) {
    perror("No Memory");
    return 0;
  }
  pthread_mutex_lock(&registry_lock);
  line->obj = obj;
  line->next = registry.first;
  registry.first = line;
  pthread_mutex_unlock(&registry_lock);
  return obj;
}

// free a single registry
//
// free only one.
static void unregister_object(VALUE obj) {
  pthread_mutex_lock(&registry_lock);
  struct Object* line = registry.first;
  struct Object* prev = NULL;
  while (line) {
    if (line->obj == obj) {
      if (line == registry.first)
        registry.first = line->next;
      else if (prev)  // must be true, really
        prev->next = line->next;
      free(line);
      goto finish;
    }
    prev = line;
    line = line->next;
  }
finish:
  pthread_mutex_unlock(&registry_lock);
}
// Replaces one registry object with another,
// allowing updates to the Registry with no memory allocations.
//
// returns 0 if all OK, returns -1 if it couldn't replace the object.
static int replace_object(VALUE obj, VALUE new_obj) {
  int ret = -1;
  pthread_mutex_lock(&registry_lock);
  struct Object* line = registry.first;
  struct Object* prev = NULL;
  while (line) {
    if (line->obj == obj) {
      line->obj = new_obj;
      ret = 0;
      goto finish;
    }
    prev = line;
    line = line->next;
  }
finish:
  pthread_mutex_unlock(&registry_lock);
  return ret;
}

// a callback for the GC (marking active objects)
static void registry_mark(void* ignore) {
  // Registry.print();
  pthread_mutex_lock(&registry_lock);
  struct Object* line = registry.first;
  while (line) {
    if (line->obj)
      rb_gc_mark(line->obj);
    line = line->next;
  }
  pthread_mutex_unlock(&registry_lock);
}

// clear the registry (end of lifetime)
static void registry_clear(void* ignore) {
  pthread_mutex_lock(&registry_lock);
  struct Object* line = registry.first;
  struct Object* to_free = NULL;
  while (line) {
    to_free = line;
    line = line->next;
    free(to_free);
  }
  registry.first = NULL;
  registry.owner = 0;
  pthread_mutex_unlock(&registry_lock);
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
  pthread_mutex_lock(&registry_lock);
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
  pthread_mutex_unlock(&registry_lock);
}

// print data, for testing
static void print(void) {
  pthread_mutex_lock(&registry_lock);
  struct Object* line = registry.first;
  fprintf(stderr, "Registry owner is %lu\n", registry.owner);
  long index = 0;
  while (line) {
    fprintf(stderr, "[%lu] => obj %lu type %d at %p\n", index++, line->obj,
            TYPE(line->obj), line);
    line = line->next;
  }
  fprintf(stderr, "Total of %lu registered objects being marked\n", index);
  pthread_mutex_unlock(&registry_lock);
}

////////////////////////////////////////////
// The API gateway
struct ___RegistryClass___ Registry = {
    .init = init,
    .remove = unregister_object,
    .add = register_object,
    .replace = replace_object,
    .print = print,
};
