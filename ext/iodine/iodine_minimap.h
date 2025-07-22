#ifndef H___IODINE_MINIMAP___H
#define H___IODINE_MINIMAP___H
#include "iodine.h"

/* *****************************************************************************
Mini Ruby Hash Map
***************************************************************************** */

static uint64_t iodinde_hmap_hash(VALUE k) {
  if (RB_TYPE_P(k, RUBY_T_STRING)) {
    fio_buf_info_s buf = (fio_buf_info_s)IODINE_RSTR_INFO(k);
    return fio_risky_hash(buf.buf, buf.len, (uint64_t)&iodinde_hmap_hash);
  }
  if (RB_SYMBOL_P(k) || RB_FIXNUM_P(k) || k == RUBY_T_TRUE ||
      k == RUBY_T_FALSE || k == RUBY_T_NIL)
    return fio_risky_num((uint64_t)k, (uint64_t)&iodinde_hmap_hash);
  rb_raise(rb_eTypeError, "key MUST be either a String or a Symbol.");
  return 0;
}

static _Bool iodinde_hmap_object_cmp(VALUE a, VALUE b) {
  if (a == b)
    return 1;
  if ((unsigned)(!RB_TYPE_P(b, RUBY_T_STRING)) | (!RB_TYPE_P(a, RUBY_T_STRING)))
    return 0;
  fio_buf_info_s aa = IODINE_RSTR_INFO(a);
  fio_buf_info_s bb = IODINE_RSTR_INFO(b);
  return FIO_BUF_INFO_IS_EQ(aa, bb);
}

#define FIO_MAP_NAME                iodine_hmap
#define FIO_MAP_KEY                 VALUE
#define FIO_MAP_VALUE               VALUE
#define FIO_MAP_HASH_FN             iodinde_hmap_hash
#define FIO_MAP_KEY_CMP(a, b)       iodinde_hmap_object_cmp(a, b)
#define FIO_MAP_KEY_COPY(dest, src) ((dest) = (src))
#include FIO_INCLUDE_FILE

typedef struct {
  iodine_hmap_s map;
  VALUE tmp;
} iodine_minimap_s;

static int iodine_minimap_gc_mark_task(iodine_hmap_each_s *e) {
  rb_gc_mark(e->key);
  rb_gc_mark(e->value);
  return 0;
}

FIO_IFUNC void iodine_minimap_gc_mark(void *m_) {
  iodine_minimap_s *m = (iodine_minimap_s *)m_;
  if (m->tmp)
    rb_gc_mark(m->tmp);
  iodine_hmap_each(&m->map, iodine_minimap_gc_mark_task, NULL, 0);
}

static VALUE iodine_minimap_store(iodine_minimap_s *m, VALUE key, VALUE value) {
  if (value == Qnil)
    goto on_delete;
  if (RB_TYPE_P(key, T_STRING) && !rb_obj_frozen_p(key)) {
    m->tmp = value;
    key = rb_str_dup_frozen(key);
    m->tmp = 0;
  }
  return iodine_hmap_set(&m->map, key, value, NULL);
on_delete:
  iodine_hmap_remove(&m->map, key, &value);
  return value;
}

FIO_IFUNC VALUE iodine_minimap_rb_get(iodine_minimap_s *m, VALUE key) {
  return iodine_hmap_get(&m->map, key);
}

typedef struct iodine_hmap_rb_each_task_s {
  size_t r;
  iodine_hmap_s *m;
} iodine_hmap_rb_each_task_s;

static int iodine_minimap_c_each_task(iodine_hmap_each_s *e) {
  iodine_hmap_rb_each_task_s *info = (iodine_hmap_rb_each_task_s *)e->udata;
  VALUE args[] = {e->key, e->value};
  info->r++;
  rb_yield_values2(2, args);
  return 0;
}
static VALUE iodine_minimap_rb_each_task(VALUE info_) {
  iodine_hmap_rb_each_task_s *info = (iodine_hmap_rb_each_task_s *)info_;
  iodine_hmap_each(info->m, iodine_minimap_c_each_task, info, 0);
  return Qnil;
}

static VALUE iodine_minimap_rb_each(iodine_minimap_s *m) {
  rb_need_block();
  if (!iodine_hmap_count(&m->map))
    return ULL2NUM(0);
  iodine_hmap_rb_each_task_s i = {.m = &m->map};
  VALUE exc;
  int e = 0;
  rb_protect(iodine_minimap_rb_each_task, (VALUE)&i, &e);
  if (e)
    goto error;
  // iodine_hmap_each(m, iodine_hmap_c_each_task, &i, 0);
  return ULL2NUM(i.r);
error:
  exc = rb_errinfo();
  if (!rb_obj_is_instance_of(exc, rb_eLocalJumpError))
    iodine_handle_exception(NULL);
  rb_set_errinfo(Qnil);
  return ULL2NUM(i.r);
}

/* *****************************************************************************
Ruby Object.
***************************************************************************** */

static void iodine_minimap_free(void *p) {
  iodine_minimap_s *m = (iodine_minimap_s *)p;
  iodine_hmap_destroy(&m->map);
  ruby_xfree(m);
  FIO_LEAK_COUNTER_ON_FREE(iodine_minimap);
}

static size_t iodine_minimap_size(const void *p) {
  iodine_minimap_s *m = (iodine_minimap_s *)p;
  return sizeof(*m) + (iodine_hmap_capa(&m->map) * sizeof(m->map.map[0]));
}

static const rb_data_type_t IODINE_MINIMAP_DATA_TYPE = {
    .wrap_struct_name = "IodineMiniMap",
    .function =
        {
            .dmark = iodine_minimap_gc_mark,
            .dfree = iodine_minimap_free,
            .dsize = iodine_minimap_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static iodine_minimap_s *iodine_minimap_ptr(VALUE self) {
  iodine_minimap_s *m;
  TypedData_Get_Struct(self, iodine_minimap_s, &IODINE_MINIMAP_DATA_TYPE, m);
  return m;
}

static VALUE iodine_minimap_alloc(VALUE klass) {

  FIO_LEAK_COUNTER_ON_ALLOC(iodine_minimap);
  iodine_minimap_s *m;
  VALUE self = TypedData_Make_Struct(klass,
                                     iodine_minimap_s,
                                     &IODINE_MINIMAP_DATA_TYPE,
                                     m);
  *m = (iodine_minimap_s)FIO_MAP_INIT;
  return self;
  // return TypedData_Wrap_Struct(klass, &IODINE_MINIMAP_DATA_TYPE, m);
}

/* *****************************************************************************
API
***************************************************************************** */

static VALUE iodine_minimap_get(VALUE o, VALUE key) {
  VALUE r = iodine_minimap_rb_get(iodine_minimap_ptr(o), key);
  if (!r)
    r = Qnil;
  return r;
}
static VALUE iodine_minimap_set(VALUE o, VALUE key, VALUE value) {
  return iodine_minimap_store(iodine_minimap_ptr(o), key, value);
}
static VALUE iodine_minimap_each(VALUE o) {
  return iodine_minimap_rb_each(iodine_minimap_ptr(o));
}
static VALUE iodine_minimap_count(VALUE o) {
  return ULL2NUM(
      (unsigned long long)iodine_hmap_count(&iodine_minimap_ptr(o)->map));
}
static VALUE iodine_minimap_clear(VALUE o) {
  iodine_hmap_clear(&iodine_minimap_ptr(o)->map);
  return o;
}

static VALUE iodine_minimap_capa(VALUE o) {
  return ULL2NUM(
      (unsigned long long)iodine_hmap_capa(&iodine_minimap_ptr(o)->map));
}
static VALUE iodine_minimap_reserve(VALUE o, VALUE s_) {
  rb_check_type(s_, T_FIXNUM);
  size_t s = (size_t)NUM2ULL(s_);
  if (s > 0x0FFFFFFFULL)
    rb_raise(
        rb_eRangeError,
        "cannot reserve negative values or values higher than 268,435,455.");
  iodine_hmap_s *m = &iodine_minimap_ptr(o)->map;
  iodine_hmap_reserve(m, s);
  return o;
}

/* *****************************************************************************
String (JSON) output
***************************************************************************** */

static int iodine___minimap_to_s_task(iodine_hmap_each_s *e) {
  VALUE tmp = e->key;
  /* Note: keys MUST be Strings  */
  switch (rb_type(tmp)) {
  case RUBY_T_SYMBOL: tmp = rb_sym2str(tmp); /* fall through */
  case RUBY_T_STRING:
  string_key:
    *(char **)e->udata = fio_bstr_write(*(char **)e->udata, "\"", 1);
    *(char **)e->udata = fio_bstr_write_escape(*(char **)e->udata,
                                               RSTRING_PTR(tmp),
                                               (size_t)RSTRING_LEN(tmp));
    *(char **)e->udata = fio_bstr_write(*(char **)e->udata, "\"", 1);
    break;
  case RUBY_T_FIXNUM:
    *(char **)e->udata = fio_bstr_write2(*(char **)e->udata,
                                         FIO_STRING_WRITE_STR2("\"", 1),
                                         FIO_STRING_WRITE_NUM(NUM2LL(tmp)),
                                         FIO_STRING_WRITE_STR2("\"", 1));
    break;
  case RUBY_T_TRUE:
    *(char **)e->udata = fio_bstr_write(*(char **)e->udata, "\"true\"", 6);
    break;
  case RUBY_T_FALSE:
    *(char **)e->udata = fio_bstr_write(*(char **)e->udata, "\"false\"", 7);
    break;
  case RUBY_T_NIL:
    *(char **)e->udata = fio_bstr_write(*(char **)e->udata, "\"null\"", 6);
    break;
  default:
    tmp = rb_any_to_s(tmp);
    if (RB_TYPE_P(tmp, RUBY_T_STRING))
      goto string_key;
    *(char **)e->udata = fio_bstr_write(*(char **)e->udata, "\"error\"", 7);
  }
  *(char **)e->udata = fio_bstr_write(*(char **)e->udata, ":", 1);
  *(char **)e->udata = iodine_json_stringify2bstr(*(char **)e->udata, e->value);
  *(char **)e->udata = fio_bstr_write(*(char **)e->udata, ",", 1);
  return 0;
}

FIO_SFUNC VALUE iodine_minimap_to_s(int argc, VALUE *argv, VALUE o) {
  VALUE r = Qnil;
  iodine_hmap_s *m = &iodine_minimap_ptr(o)->map;
  char *str;
  if (!iodine_hmap_count(m))
    goto empty;
  str = fio_bstr_reserve(NULL, ((size_t)1 << 12) - 64);
  str = fio_bstr_write(str, "{", 1);
  iodine_hmap_each(m, iodine___minimap_to_s_task, &str, 0);
  str[fio_bstr_len(str) - 1] = '}';
  r = rb_str_new(str, (long)fio_bstr_len(str));
  fio_bstr_free(str);
  return r;
empty:
  r = rb_str_new("{}", 2);
  return r;
  (void)argc, (void)argv;
}

/* *****************************************************************************
Benchmark C world performance
***************************************************************************** */

static int each_task(void *_i) {
  FIO_COMPILER_GUARD;
  return 0;
  (void)_i;
}

#define FIO_MAP_NAME          iodine_mini_map
#define FIO_MAP_KEY           size_t
#define FIO_MAP_KEY_CMP(a, b) ((a) == (b))
#define FIO_MAP_VALUE         size_t
#define FIO_MAP_HASH_FN(k)    (fio_risky_num(k, 0))
#include FIO_INCLUDE_FILE

typedef VALUE ruby_hash_s;
FIO_IFUNC void ruby_hash_destroy(VALUE *m) {
  if (!*m)
    return;
  STORE.release(*m);
  *m = 0;
}
FIO_IFUNC size_t ruby_hash_reserve(VALUE *m, size_t capa) {
  *m = rb_hash_new();
  STORE.hold(*m);
  (void)capa;
  return capa;
}

FIO_IFUNC size_t ruby_hash_count(VALUE *m) { return RHASH_SIZE(*m); }
FIO_IFUNC size_t ruby_hash_capa(VALUE *m) {
  return rb_st_memsize(RHASH_TBL(*m)) / (sizeof(VALUE) * 3);
}
FIO_IFUNC VALUE ruby_hash_set(VALUE *m, size_t k, size_t v, size_t *old) {
  rb_hash_aset(*m, k, v);
  return v;
  (void)old;
}
FIO_IFUNC size_t ruby_hash_get(VALUE *m, size_t o) {
  VALUE tmp = rb_hash_aref(*m, o);
  if (tmp == Qnil)
    return 0;
  return tmp;
}

FIO_SFUNC int ruby_hash_each___task(VALUE m, VALUE k, VALUE v) {
  FIO_COMPILER_GUARD;
  return ST_CONTINUE;
  (void)m, (void)k, (void)v;
}
typedef struct {
  int i;
} ruby_hash_each_s;
FIO_IFUNC size_t ruby_hash_each(VALUE *m,
                                int (*each_task)(ruby_hash_each_s *),
                                void *udata,
                                ssize_t start_at) {
  rb_hash_foreach(*m, ruby_hash_each___task, (VALUE)udata);
  return RHASH_SIZE((*m));
  (void)start_at, (void)each_task;
}

static VALUE iodine_minimap_benchmark_c(VALUE klass, VALUE object_count) {
  (void)klass;
  if (object_count == Qnil)
    object_count = ULONG2NUM(30UL);
  if (NUM2ULL(object_count) > 10000000UL)
    rb_raise(rb_eRangeError, "object count is too high.");
  const size_t ORDERED_OBJECTS = 0U;
  const size_t RANDOM_OBJECTS = NUM2ULL(object_count);
  const size_t OBJECTS = ORDERED_OBJECTS + RANDOM_OBJECTS;
  const size_t CYCLES = 10000000U / OBJECTS;
  const size_t TESTS = 3U;
  const size_t FIND_CYCLES = CYCLES;
  const size_t EACH_CYCLES = CYCLES;
  const size_t MISSING_OBJECTS = OBJECTS;
  size_t *numbers = malloc(sizeof(*numbers) * (OBJECTS + MISSING_OBJECTS));
  { /* make sure all objects are unique */
    iodine_mini_map_s existing = {0};
    for (size_t i = 1; i < ORDERED_OBJECTS + 1; ++i) {
      numbers[i - 1] = i;
      iodine_mini_map_set(&existing, i - i, i, NULL);
    }
    for (size_t i = ORDERED_OBJECTS; i < (OBJECTS + MISSING_OBJECTS); ++i) {
      do {
        numbers[i] = fio_rand64();
        numbers[i] >>= 32;
        numbers[i] = ULL2NUM(numbers[i]);
      } while (iodine_mini_map_get_ptr(&existing, numbers[i]));
      numbers[i] += !numbers[i];
      iodine_mini_map_set(&existing, numbers[i], numbers[i], NULL);
    }
    iodine_mini_map_destroy(&existing);
  }
#define PERFORM_TEST(klass)                                                    \
  do {                                                                         \
    klass##_s m = {0};                                                         \
    uint64_t insert, overwrite, find, loop, find_missing, start;               \
    start = fio_time_micro();                                                  \
    start = fio_time_micro();                                                  \
    for (size_t j = 0; j < CYCLES; ++j) {                                      \
      klass##_destroy(&m);                                                     \
      if (1)                                                                   \
        klass##_reserve(&m, 8 | ((OBJECTS) >> 31));                            \
      for (size_t i = 0; i < OBJECTS; ++i) {                                   \
        klass##_set(&m, numbers[i], numbers[i], NULL);                         \
      }                                                                        \
    }                                                                          \
    insert = fio_time_micro() - start;                                         \
    if (klass##_count(&m) != OBJECTS)                                          \
      FIO_LOG_ERROR("map counter error (%zu != %zu)!",                         \
                    klass##_count(&m),                                         \
                    OBJECTS);                                                  \
    if (klass##_capa(&m) < OBJECTS)                                            \
      FIO_LOG_ERROR("map capacity error (%zu < %zu)!",                         \
                    klass##_capa(&m),                                          \
                    OBJECTS);                                                  \
    start = fio_time_micro();                                                  \
    for (size_t j = 0; j < CYCLES; ++j) {                                      \
      for (size_t i = 0; i < OBJECTS; ++i)                                     \
        klass##_set(&m, numbers[i], numbers[i], NULL);                         \
    }                                                                          \
    overwrite = fio_time_micro() - start;                                      \
    if (klass##_count(&m) != OBJECTS)                                          \
      FIO_LOG_ERROR("map counter error (%zu != %zu)!",                         \
                    klass##_count(&m),                                         \
                    OBJECTS);                                                  \
    if (klass##_capa(&m) < OBJECTS)                                            \
      FIO_LOG_ERROR("map capacity error (%zu < %zu)!",                         \
                    klass##_capa(&m),                                          \
                    OBJECTS);                                                  \
    start = fio_time_micro();                                                  \
    for (size_t sc = 0; sc < FIND_CYCLES; ++sc)                                \
      for (size_t i = 0; i < OBJECTS; ++i) {                                   \
        if (klass##_get(&m, numbers[i]) != numbers[i])                         \
          FIO_LOG_ERROR(FIO_MACRO2STR(klass) "_get error ([%zu] %zu != %zu)!", \
                        i,                                                     \
                        numbers[i],                                            \
                        klass##_get(&m, i));                                   \
      }                                                                        \
    find = fio_time_micro() - start;                                           \
    start = fio_time_micro();                                                  \
    for (size_t sc = 0; sc < FIND_CYCLES; ++sc)                                \
      for (size_t i = OBJECTS; i < (OBJECTS + MISSING_OBJECTS); ++i) {         \
        if (klass##_get(&m, numbers[i]))                                       \
          FIO_LOG_ERROR(                                                       \
              FIO_MACRO2STR(klass) "_get error"                                \
                                   "([%zu] %zu shouldn't exist but == %zu)!",  \
              i,                                                               \
              numbers[i],                                                      \
              klass##_get(&m, numbers[i]));                                    \
      }                                                                        \
    find_missing = fio_time_micro() - start;                                   \
    start = fio_time_micro();                                                  \
    for (size_t i = 0; i < EACH_CYCLES; ++i)                                   \
      klass##_each(&m, (int (*)(klass##_each_s *))each_task, NULL, 0);         \
    loop = fio_time_micro() - start;                                           \
    FIO_LOG_INFO("  %-16s"                                                     \
                 "\tcapa: %zu/%-6zu"                                           \
                 "\tinsert: %-6zu"                                             \
                 "\toverwrite: %-6zu"                                          \
                 "\tfind: %-6zu"                                               \
                 "\tfind missing: %-6zu"                                       \
                 "\tloop: %-6zu",                                              \
                 FIO_MACRO2STR(klass),                                         \
                 (size_t)klass##_count(&m),                                    \
                 (size_t)klass##_capa(&m),                                     \
                 insert,                                                       \
                 overwrite,                                                    \
                 find,                                                         \
                 find_missing,                                                 \
                 loop);                                                        \
    klass##_destroy(&m);                                                       \
  } while (0);
  for (size_t tst = 0; tst < TESTS; ++tst) {
    PERFORM_TEST(iodine_mini_map)
    PERFORM_TEST(ruby_hash)
  }
  free(numbers);
  return Qnil;
}

/* *****************************************************************************
Initialize

Benchmark with:

require 'iodine/benchmark'
Iodine::Benchmark.minimap(100)

m = Iodine::Base::MiniMap.new
10.times {|i| m[i] = i }
m.each {|k,v| puts "#{k.to_s} => #{v.to_s}"}
***************************************************************************** */
static void Init_Iodine_MiniMap(void) {
  VALUE m = rb_define_class_under(iodine_rb_IODINE_BASE, "MiniMap", rb_cObject);
  rb_define_alloc_func(m, iodine_minimap_alloc);
  rb_define_method(m, "[]", iodine_minimap_get, 1);
  rb_define_method(m, "[]=", iodine_minimap_set, 2);
  rb_define_method(m, "clear", iodine_minimap_clear, 0);
  rb_define_method(m, "count", iodine_minimap_count, 0);
  rb_define_method(m, "capa", iodine_minimap_capa, 0);
  rb_define_method(m, "each", iodine_minimap_each, 0);
  rb_define_method(m, "reserve", iodine_minimap_reserve, 1);
  rb_define_method(m, "to_s", iodine_minimap_to_s, -1);
  rb_define_method(m, "to_json", iodine_minimap_to_s, -1);
  rb_define_singleton_method(m, "cbench", iodine_minimap_benchmark_c, 1);
}
#endif /* H___IODINE_MINIMAP___H */
