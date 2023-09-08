#ifndef H___IODINE_STORE___H
#define H___IODINE_STORE___H
#include "iodine.h"
/* *****************************************************************************
Ruby Object Storage (GC management)

Use:

// Adds a Ruby Object from the store, holding it against GC cleanup
STORE.hold(VALUE o);

// Removed a Ruby Object from the store, releasing it's GC hold
STORE.release(VALUE o);

// Performs callback during every GC cycle
STORE.on_gc(void (*fn)(void *), void *arg);

***************************************************************************** */

/* *****************************************************************************
Ruby Garbage Collection Protection Object
***************************************************************************** */

typedef struct {
  void (*fn)(void *);
  void *arg;
} store___task_s;

#define FIO_ARRAY_NAME           store___todo
#define FIO_ARRAY_TYPE           store___task_s
#define FIO_ARRAY_TYPE_CMP(a, b) ((a).fn == (b).fn && (a).arg == (b).arg)
#define FIO_ARRAY_EXPONENTIAL    1
#define FIO_MAP_NAME             iodine_reference_store_map
#define FIO_MAP_KEY              VALUE
#define FIO_MAP_VALUE            size_t
#define FIO_MAP_HASH_FN(o)       fio_risky_ptr((void *)(o))
#define FIO_THREADS
#include FIO_INCLUDE_FILE

FIO_SFUNC void value_reference_counter_store_hold(VALUE o);
FIO_SFUNC void value_reference_counter_store_release(VALUE o);
FIO_SFUNC void value_reference_counter_store_on_gc(void (*fn)(void *),
                                                   void *arg);

static struct value_reference_counter_store_s {
  iodine_reference_store_map_s map;
  store___todo_s todo;
  fio_thread_mutex_t lock;
  /** Adds a VALUE to the store, protecting it from the GC. */
  void (*hold)(VALUE);
  /** Removed a VALUE to the store, if it's `hold` count drops to zero. */
  void (*release)(VALUE);
  /** Adds a task to be performed during the next GC cycle. */
  void (*on_gc)(void (*fn)(void *), void *arg);
} STORE = {
    FIO_MAP_INIT,
    FIO_ARRAY_INIT,
    FIO_THREAD_MUTEX_INIT,
    value_reference_counter_store_hold,
    value_reference_counter_store_release,
    value_reference_counter_store_on_gc,
};

/**
 * Prints the number of object withheld from the GC (for debugging).
 *
 *     Iodine::Base.print_debug
 */
FIO_SFUNC VALUE value_reference_counter_store_print_debug(VALUE self) {
  fprintf(
      stderr,
      "C-Ruby Bridge: Objects withheld from GC: %zu (%zu current capacity)\n"
      "               Tasks to do: %zu\n",
      (size_t)iodine_reference_store_map_count(&STORE.map),
      (size_t)iodine_reference_store_map_capa(&STORE.map),
      (size_t)store___todo_count(&STORE.todo));
  return self;
}

FIO_IFUNC void store___todo_perform_tasks_unsafe(
    struct value_reference_counter_store_s *s) {
  store___task_s tsk = {NULL};
  while (!store___todo_pop(&s->todo, &tsk))
    tsk.fn(tsk.arg);
}

FIO_SFUNC void value_reference_counter_store_hold(VALUE o) {
  if (!o || o == Qnil || o == Qtrue || o == Qfalse || TYPE(o) == RUBY_T_FIXNUM)
    return;
  fio_thread_mutex_lock(&STORE.lock);
  iodine_reference_store_map_node_s *n =
      iodine_reference_store_map_get_ptr(&STORE.map, o);
  if (!n) {
    iodine_reference_store_map_set(&STORE.map, o, 1, NULL);
  } else {
    fio_atomic_add(&n->value, 1);
  }
  fio_thread_mutex_unlock(&STORE.lock);
}
FIO_SFUNC void value_reference_counter_store_release(VALUE o) {
  if (!o || o == Qnil || o == Qtrue || o == Qfalse || TYPE(o) == RUBY_T_FIXNUM)
    return;
  fio_thread_mutex_lock(&STORE.lock);
  iodine_reference_store_map_node_s *n =
      iodine_reference_store_map_get_ptr(&STORE.map, o);
  if (n && !fio_atomic_sub_fetch(&n->value, 1)) {
    iodine_reference_store_map_remove(&STORE.map, o, NULL);
  }
  fio_thread_mutex_unlock(&STORE.lock);
}

FIO_SFUNC void value_reference_counter_store_on_gc(void (*fn)(void *),
                                                   void *arg) {
  if (!fn && !arg)
    return;
  fio_thread_mutex_lock(&STORE.lock);
  store___todo_push(&STORE.todo, (store___task_s){.fn = fn, .arg = arg});
  fio_thread_mutex_unlock(&STORE.lock);
}

FIO_SFUNC void value_reference_counter_store_gc_mark(
    struct value_reference_counter_store_s *s) {
  if (!iodine_reference_store_map_count(&s->map) &&
      !store___todo_count(&s->todo))
    return;
  fio_thread_mutex_lock(&s->lock);
  store___todo_perform_tasks_unsafe(s);
  FIO_MAP_EACH(iodine_reference_store_map, &s->map, i) { rb_gc_mark(i.key); }
  fio_thread_mutex_unlock(&s->lock);
}
FIO_SFUNC void value_reference_counter_store_destroy(
    struct value_reference_counter_store_s *s) {
  fio_thread_mutex_destroy(&s->lock);
  store___todo_perform_tasks_unsafe(s);
  iodine_reference_store_map_destroy(&s->map);
  store___todo_destroy(&s->todo);
}

static void iodine_setup_value_reference_counter(VALUE klass) {
  static const struct rb_data_type_struct storage_type_struct = {
      .wrap_struct_name = "CRubyReferenceStore",
      .function.dfree = (void (*)(void *))value_reference_counter_store_destroy,
      .function.dmark = (void (*)(void *))value_reference_counter_store_gc_mark,
  };
  static VALUE keep_alive = 0;
  if (keep_alive)
    return;
  /** Container Class for Ruby-C bridge (GC protected objects). */
  rb_define_singleton_method(klass,
                             "print_debug",
                             value_reference_counter_store_print_debug,
                             0);
  keep_alive = TypedData_Wrap_Struct(klass, &storage_type_struct, &STORE);
  rb_global_variable(&keep_alive);
  fio_state_callback_add(
      FIO_CALL_AT_EXIT,
      (void (*)(void *))value_reference_counter_store_destroy,
      (void *)&STORE);
}

#endif
