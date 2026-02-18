#if !defined(H___IODINE_STORE___H)
#define H___IODINE_STORE___H
#if !defined(IODINE_STORE_SKIP_PRINT)
#include "iodine.h"
#endif
/* *****************************************************************************
Ruby Object Storage (GC management)

Use:

// Adds a Ruby Object from the store, holding it against GC cleanup
STORE.hold(VALUE o);

// Removed a Ruby Object from the store, releasing it's GC hold
STORE.release(VALUE o);

// Performs callback during every GC cycle
STORE.on_gc(void (*fn)(void *), void *arg);

// Returns a frozen String object (from cache, if exists)
VALUE str = STORE.frozen_str(fio_buf_info_s cstr);

// Returns a frozen Header String object (from cache, if exists)
VALUE str = STORE.header_name(fio_buf_info_s header_name);


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

#define FIO_MAP_KEY_KSTR
#define FIO_MAP_HASH_FN(o)                                                     \
  fio_risky_hash((o).buf, (o).len, (uint64_t)&iodine_rb_IODINE);
#define FIO_MAP_NAME  iodine_reference_store_frzn
#define FIO_MAP_VALUE VALUE
#include FIO_INCLUDE_FILE

FIO_SFUNC void iodine_store___gc_stop(void);
FIO_SFUNC void iodine_store___gc_start(void);
FIO_SFUNC void iodine_store___hold(VALUE o);
FIO_SFUNC void iodine_store___release(VALUE o);
FIO_SFUNC void iodine_store___on_gc(void (*fn)(void *), void *arg);
FIO_SFUNC VALUE iodine_store___frozen_str(fio_str_info_s n);
FIO_SFUNC VALUE iodine_store___header_name(fio_str_info_s n);
FIO_SFUNC void iodine_store___destroy(void);

static struct value_reference_counter_store_s {
  iodine_reference_store_map_s map;
  iodine_reference_store_frzn_s frozen;
  iodine_reference_store_frzn_s headers;
  store___todo_s todo;
  size_t limit;
  size_t gc_stop_counter;
  fio_thread_mutex_t lock;
  /** Adds a VALUE to the store, protecting it from the GC. */
  void (*const hold)(VALUE);
  /** Removed a VALUE to the store, if it's `hold` count drops to zero. */
  void (*const release)(VALUE);
  /** Stops the Garbage Collector, or increases the stop count. */
  void (*const gc_stop)(void);
  /** Decreases the `gc_stop` count and re-starts Garbage Collector. */
  void (*const gc_start)(void);
  /** Adds a task to be performed during the next GC cycle. */
  void (*const on_gc)(void (*fn)(void *), void *arg);
  /** Returns a frozen String, possibly cached. */
  VALUE (*const frozen_str)(fio_str_info_s);
  /** Returns a frozen String header name (`HTTP_` + uppercase). */
  VALUE (*const header_name)(fio_str_info_s);
  /* releases all objects */
  void (*const destroy)(void);
  VALUE single_use[256];
} STORE = {
    FIO_MAP_INIT,
    FIO_MAP_INIT,
    FIO_MAP_INIT,
    FIO_ARRAY_INIT,
    228,
    0,
    FIO_THREAD_MUTEX_INIT,
    iodine_store___hold,
    iodine_store___release,
    iodine_store___gc_stop,
    iodine_store___gc_start,
    iodine_store___on_gc,
    iodine_store___frozen_str,
    iodine_store___header_name,
    iodine_store___destroy,
};

FIO_SFUNC void iodine_store___gc_stop(void) {
  size_t state = fio_atomic_add(&STORE.gc_stop_counter, 1);
  if (state)
    return;
  FIO_LOG_DEBUG("GC Paused.");
  rb_gc_disable();
}
FIO_SFUNC void iodine_store___gc_start(void) {
  size_t state = fio_atomic_sub(&STORE.gc_stop_counter, 1);
  if (state + 256 > 256)
    return;
  if (state + 256 < 256) {
    fio_atomic_add(&STORE.gc_stop_counter, 1);
    return;
  }
  FIO_LOG_DEBUG("GC Resumed.");
  rb_gc_enable();
}

/**
 * Prints the number of object withheld from the GC (for debugging).
 *
 *     Iodine::Base.print_debug
 */
FIO_SFUNC VALUE iodine_store___print_debug(VALUE self) {
#ifndef IODINE_STORE_SKIP_PRINT
  fprintf(
      stderr,
      "DEBUG: (%d) Iodine-Ruby memory store info:\n"
      "      \tRuby Objects Held:     %-4zu       (%-4zu current capacity)\n"
      "      \tCached Frozen Strings: %-4zu/%-4zu (%-4zu capacity)\n"
      "      \tCached Rack Headers:   %-4zu/%-4zu (%-4zu capacity)\n"
      "      \tTasks to do:           %-4zu\n"
#if FIO_LEAK_COUNTER
      "      \tIodine Objects Allocated:\n"
      "      \tConnections:           %-4zu\tMiniMaps:       %-4zu\n"
      "      \tMustache:              %-4zu\tPubSubMessages: %-4zu\n"
      "      \tfacil.io Objects Allocated:\n"
      "      \tHTTP Handles:          %-4zu\tfio_bstr_s:     %-4zu\n"
      "      \tIO Objects:            %-4zu\n"
#endif /* FIO_LEAK_COUNTER */
      ,
      (int)fio_getpid(),
      (size_t)iodine_reference_store_map_count(&STORE.map),
      (size_t)iodine_reference_store_map_capa(&STORE.map),
      (size_t)iodine_reference_store_frzn_count(&STORE.frozen),
      STORE.limit,
      (size_t)iodine_reference_store_frzn_capa(&STORE.frozen),
      (size_t)iodine_reference_store_frzn_count(&STORE.headers),
      STORE.limit,
      (size_t)iodine_reference_store_frzn_capa(&STORE.headers),
      (size_t)store___todo_count(&STORE.todo)
#if FIO_LEAK_COUNTER
          ,
      FIO_LEAK_COUNTER_COUNT(iodine_connection),
      FIO_LEAK_COUNTER_COUNT(iodine_minimap),
      FIO_LEAK_COUNTER_COUNT(iodine_mustache),
      FIO_LEAK_COUNTER_COUNT(iodine_pubsub_msg),
      FIO_LEAK_COUNTER_COUNT(fio_http),
      FIO_LEAK_COUNTER_COUNT(fio_bstr_s),
      FIO_LEAK_COUNTER_COUNT(fio___io)
#endif /* FIO_LEAK_COUNTER */
  );
  // fio_state_callback_print_state();
#endif /* IODINE_STORE_SKIP_PRINT */
  return self;
}

/** Sets Iodine's cache limit for frozen strings, limited to 65,535 items. */
FIO_SFUNC VALUE iodine_store___cache_limit_set(VALUE self, VALUE nlim) {
  Check_Type(nlim, T_FIXNUM);
  unsigned long long l = NUM2ULL(nlim);
  if (l > 65536)
    l = 65536;
  STORE.limit = l;
  return ULL2NUM(l);
}
/** Gets Iodine's cache limit for frozen strings. */
FIO_SFUNC VALUE iodine_store___cache_limit_get(VALUE self) {
  return ULL2NUM((unsigned long long)STORE.limit);
}

FIO_IFUNC void store___todo_perform_tasks_unsafe(
    struct value_reference_counter_store_s *s) {
  store___task_s tsk = {NULL};
  while (!store___todo_pop(&s->todo, &tsk))
    tsk.fn(tsk.arg);
}

FIO_IFUNC void iodine_store___hold_unsafe(VALUE o) {
  iodine_reference_store_map_node_s *n =
      iodine_reference_store_map_get_ptr(&STORE.map, o);
  if (!n) {
    iodine_reference_store_map_set(&STORE.map, o, 1, NULL);
  } else {
    fio_atomic_add(&n->value, 1);
  }
}

FIO_SFUNC void iodine_store___hold(VALUE o) {
  if (IODINE_STORE_IS_SKIP(o))
    return;
  fio_thread_mutex_lock(&STORE.lock);
  iodine_store___hold_unsafe(o);
  fio_thread_mutex_unlock(&STORE.lock);
}
FIO_SFUNC void iodine_store___release(VALUE o) {
  if (IODINE_STORE_IS_SKIP(o) || !iodine_reference_store_map_count(&STORE.map))
    return;
  fio_thread_mutex_lock(&STORE.lock);
  iodine_reference_store_map_node_s *n =
      iodine_reference_store_map_get_ptr(&STORE.map, o);
  if (n && !fio_atomic_sub_fetch(&n->value, 1)) {
    iodine_reference_store_map_remove(&STORE.map, o, NULL);
  }
  fio_thread_mutex_unlock(&STORE.lock);
}

FIO_SFUNC VALUE iodine_store___frozen_str(fio_str_info_s n) {
  VALUE r;
  fio_thread_mutex_lock(&STORE.lock);
  r = iodine_reference_store_frzn_get(&STORE.frozen, n);
  fio_thread_mutex_unlock(&STORE.lock);
  if (r)
    return r;

  r = rb_str_new(n.buf, n.len); /* might invoke GC, can't be in a lock */
  rb_str_freeze(r);
  if (iodine_reference_store_frzn_count(&STORE.frozen) < STORE.limit) {
    /* May cause STORE.frozen to grow slightly more than limit, acceptable */
    fio_thread_mutex_lock(&STORE.lock);
    iodine_reference_store_frzn_set(&STORE.frozen, n, r, NULL);
    fio_thread_mutex_unlock(&STORE.lock);
  }
  return r;
}

FIO_SFUNC VALUE iodine_store___header_name(fio_str_info_s n) {
  VALUE r;
  size_t offset;
  if (!n.len || n.len > 1023)
    return Qnil;
  FIO_STR_INFO_TMP_VAR(buffer, 1152);
  /* CONTENT_LENGTH and CONTENT_TYPE should be copied without HTTP_ */
  const uint64_t content_length1 = fio_buf2u64u("content-");
  const uint64_t content_length2 = fio_buf2u64u("t-length");
  const uint64_t content_type1 = fio_buf2u64u("content-");
  const uint32_t content_type2 = fio_buf2u32u("type");

  fio_thread_mutex_lock(&STORE.lock);
  r = iodine_reference_store_frzn_get(&STORE.headers, n);
  fio_thread_mutex_unlock(&STORE.lock);
  if (r)
    return r;

  offset = 5 * !((n.len == 14 && fio_buf2u64u(n.buf) == content_length1 &&
                  fio_buf2u64u(n.buf + 6) == content_length2) ||
                 (n.len == 12 && fio_buf2u64u(n.buf) == content_type1 &&
                  fio_buf2u32u(n.buf + 8) == content_type2));
  fio_string_write2(&buffer,
                    NULL,
                    FIO_STRING_WRITE_STR2("HTTP_", offset),
                    FIO_STRING_WRITE_STR_INFO(n));
  for (size_t i = offset; i < buffer.len; ++i) {
    if (buffer.buf[i] >= 'a' && buffer.buf[i] <= 'z')
      buffer.buf[i] = buffer.buf[i] ^ 32;
    else if (buffer.buf[i] == '-')
      buffer.buf[i] = '_';
  }
  r = rb_str_new(buffer.buf, buffer.len);
  rb_str_freeze(r);
  if (iodine_reference_store_frzn_count(&STORE.headers) < STORE.limit) {
    fio_thread_mutex_lock(&STORE.lock);
    iodine_reference_store_frzn_set(&STORE.headers, n, r, NULL);
    fio_thread_mutex_unlock(&STORE.lock);
  }
  return r;
}

FIO_SFUNC void iodine_store___destroy(void) {
  fio_thread_mutex_destroy(&STORE.lock);
  store___todo_perform_tasks_unsafe(&STORE);
  iodine_reference_store_map_destroy(&STORE.map);
  iodine_reference_store_frzn_destroy(&STORE.frozen);
  iodine_reference_store_frzn_destroy(&STORE.headers);
  store___todo_destroy(&STORE.todo);
  STORE.map = (iodine_reference_store_map_s){0};
  STORE.frozen = (iodine_reference_store_frzn_s){0};
  STORE.headers = (iodine_reference_store_frzn_s){0};
  STORE.todo = (store___todo_s){0};
  STORE.lock = (fio_thread_mutex_t)FIO_THREAD_MUTEX_INIT;
}

FIO_SFUNC void iodine_store___on_gc(void (*fn)(void *), void *arg) {
  if (!fn && !arg)
    return;
  fio_thread_mutex_lock(&STORE.lock);
  store___todo_push(&STORE.todo, (store___task_s){.fn = fn, .arg = arg});
  fio_thread_mutex_unlock(&STORE.lock);
}

FIO_SFUNC int iodine_store___gc_mark__key(
    iodine_reference_store_map_each_s *e) {
  rb_gc_mark(e->key);
  return 0;
}
FIO_SFUNC int iodine_store___gc_mark__val(
    iodine_reference_store_frzn_each_s *e) {
  rb_gc_mark(e->value);
  return 0;
}

FIO_SFUNC void iodine_store___gc_mark(
    struct value_reference_counter_store_s *s) {
  if (!s)
    return;
  if (!iodine_reference_store_map_count(&s->map) &&
      !store___todo_count(&s->todo))
    return;
  // we can skip the lock, as the GC freezes all other actions
  // fio_thread_mutex_lock(&s->lock);
  store___todo_perform_tasks_unsafe(s);
  iodine_reference_store_map_each(&s->map,
                                  iodine_store___gc_mark__key,
                                  NULL,
                                  0);
  iodine_reference_store_frzn_each(&s->frozen,
                                   iodine_store___gc_mark__val,
                                   NULL,
                                   0);
  iodine_reference_store_frzn_each(&s->headers,
                                   iodine_store___gc_mark__val,
                                   NULL,
                                   0);
  // we can skip the lock, as the GC freezes all other actions
  // fio_thread_mutex_unlock(&s->lock);
  if (FIO_LOG_LEVEL >= FIO_LOG_LEVEL_DEBUG)
    iodine_store___print_debug(Qnil);
}
FIO_SFUNC void value_reference_counter_store_destroy(VALUE i_) {
  (void)i_;
  iodine_store___destroy();
}

static void iodine_setup_value_reference_counter(VALUE klass) {
  static const struct rb_data_type_struct storage_type_struct = {
      .wrap_struct_name = "CRubyReferenceStore",
      .function.dfree = (void (*)(void *))value_reference_counter_store_destroy,
      .function.dmark = (void (*)(void *))iodine_store___gc_mark,
  };
  static VALUE keep_alive = 0;
  if (keep_alive)
    return;
  /** Container Class for Ruby-C bridge (GC protected objects). */
  rb_define_singleton_method(klass,
                             "print_debug",
                             iodine_store___print_debug,
                             0);
  rb_define_singleton_method(klass,
                             "cache_limit=",
                             iodine_store___cache_limit_set,
                             1);
  rb_define_singleton_method(klass,
                             "cache_limit",
                             iodine_store___cache_limit_get,
                             0);
  keep_alive = TypedData_Wrap_Struct(klass, &storage_type_struct, &STORE);
  rb_global_variable(&keep_alive);
  fio_state_callback_add(
      FIO_CALL_AT_EXIT,
      (void (*)(void *))value_reference_counter_store_destroy,
      (void *)&STORE);
}

#endif /* H___IODINE_STORE___H */
