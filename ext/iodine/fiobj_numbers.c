/*
Copyright: Boaz Segev, 2017-2019
License: MIT
*/

#include <fiobj_numbers.h>
#include <fiobject.h>

#include <fio.h>

#include <assert.h>
#include <errno.h>
#include <math.h>

#include <pthread.h>

/* *****************************************************************************
Numbers Type
***************************************************************************** */

typedef struct {
  fiobj_object_header_s head;
  intptr_t i;
} fiobj_num_s;

typedef struct {
  fiobj_object_header_s head;
  double f;
} fiobj_float_s;

#define obj2num(o) ((fiobj_num_s *)FIOBJ2PTR(o))
#define obj2float(o) ((fiobj_float_s *)FIOBJ2PTR(o))

/* *****************************************************************************
Numbers VTable
***************************************************************************** */

static pthread_key_t num_vt_buffer_key;
static pthread_once_t num_vt_buffer_once = PTHREAD_ONCE_INIT;
static void init_num_vt_buffer_key(void) {
  pthread_key_create(&num_vt_buffer_key, free);
}
static void init_num_vt_buffer_ptr(void) {
  char *num_vt_buffer = malloc(sizeof(char)*512);
  FIO_ASSERT_ALLOC(num_vt_buffer);
  memset(num_vt_buffer, 0, sizeof(char)*512);
  pthread_setspecific(num_vt_buffer_key, num_vt_buffer);
}

static intptr_t fio_i2i(const FIOBJ o) { return obj2num(o)->i; }
static intptr_t fio_f2i(const FIOBJ o) {
  return (intptr_t)floorl(obj2float(o)->f);
}
static double fio_i2f(const FIOBJ o) { return (double)obj2num(o)->i; }
static double fio_f2f(const FIOBJ o) { return obj2float(o)->f; }

static size_t fio_itrue(const FIOBJ o) { return (obj2num(o)->i != 0); }
static size_t fio_ftrue(const FIOBJ o) { return (obj2float(o)->f != 0); }

static fio_str_info_s fio_i2str(const FIOBJ o) {
  pthread_once(&num_vt_buffer_once, init_num_vt_buffer_key);
  char *num_buffer = pthread_getspecific(num_vt_buffer_key);
  if (!num_buffer) {
    init_num_vt_buffer_ptr();
    num_buffer = pthread_getspecific(num_vt_buffer_key);
  }
  return (fio_str_info_s){
      .data = num_buffer,
      .len = fio_ltoa(num_buffer, obj2num(o)->i, 10),
  };
}
static fio_str_info_s fio_f2str(const FIOBJ o) {
  if (isnan(obj2float(o)->f))
    return (fio_str_info_s){.data = (char *)"NaN", .len = 3};
  else if (isinf(obj2float(o)->f)) {
    if (obj2float(o)->f > 0)
      return (fio_str_info_s){.data = (char *)"Infinity", .len = 8};
    else
      return (fio_str_info_s){.data = (char *)"-Infinity", .len = 9};
  }
  pthread_once(&num_vt_buffer_once, init_num_vt_buffer_key);
  char *num_buffer = pthread_getspecific(num_vt_buffer_key);
  if (!num_buffer) {
    init_num_vt_buffer_ptr();
    num_buffer = pthread_getspecific(num_vt_buffer_key);
  }
  return (fio_str_info_s){
      .data = num_buffer,
      .len = fio_ftoa(num_buffer, obj2float(o)->f, 10),
  };
}

static size_t fiobj_i_is_eq(const FIOBJ self, const FIOBJ other) {
  return obj2num(self)->i == obj2num(other)->i;
}
static size_t fiobj_f_is_eq(const FIOBJ self, const FIOBJ other) {
  return obj2float(self)->f == obj2float(other)->f;
}

void fiobject___simple_dealloc(FIOBJ o, void (*task)(FIOBJ, void *), void *arg);
uintptr_t fiobject___noop_count(FIOBJ o);

const fiobj_object_vtable_s FIOBJECT_VTABLE_NUMBER = {
    .class_name = "Number",
    .to_i = fio_i2i,
    .to_f = fio_i2f,
    .to_str = fio_i2str,
    .is_true = fio_itrue,
    .is_eq = fiobj_i_is_eq,
    .count = fiobject___noop_count,
    .dealloc = fiobject___simple_dealloc,
};

const fiobj_object_vtable_s FIOBJECT_VTABLE_FLOAT = {
    .class_name = "Float",
    .to_i = fio_f2i,
    .to_f = fio_f2f,
    .is_true = fio_ftrue,
    .to_str = fio_f2str,
    .is_eq = fiobj_f_is_eq,
    .count = fiobject___noop_count,
    .dealloc = fiobject___simple_dealloc,
};

/* *****************************************************************************
Number API
***************************************************************************** */

/** Creates a Number object. Remember to use `fiobj_free`. */
FIOBJ fiobj_num_new_bignum(intptr_t num) {
  fiobj_num_s *o = fio_malloc(sizeof(*o));
  if (!o) {
    perror("ERROR: fiobj number couldn't allocate memory");
    exit(errno);
  }
  *o = (fiobj_num_s){
      .head =
          {
              .type = FIOBJ_T_NUMBER,
              .ref = 1,
          },
      .i = num,
  };
  return (FIOBJ)o;
}

/** Mutates a Big Number object's value. Effects every object's reference! */
// void fiobj_num_set(FIOBJ target, intptr_t num) {
//   assert(FIOBJ_TYPE_IS(target, FIOBJ_T_NUMBER) &&
//   FIOBJ_IS_ALLOCATED(target)); obj2num(target)->i = num;
// }

static pthread_key_t num_ret_key;
static pthread_once_t num_ret_once = PTHREAD_ONCE_INIT;
static void init_num_ret_key(void) {
  pthread_key_create(&num_ret_key, free);
}
static void init_num_ret_ptr(void) {
  fiobj_num_s *ret = malloc(sizeof(fiobj_num_s));
  FIO_ASSERT_ALLOC(ret);
  memset(ret, 0, sizeof(fiobj_num_s));
  pthread_setspecific(num_ret_key, ret);
}
/** Creates a temporary Number object. This ignores `fiobj_free`. */
FIOBJ fiobj_num_tmp(intptr_t num) {
  pthread_once(&num_ret_once, init_num_ret_key);
  fiobj_num_s *ret = pthread_getspecific(num_ret_key);
  if (!ret) {
    init_num_ret_ptr();
    ret = pthread_getspecific(num_ret_key);
  }
  *ret = (fiobj_num_s){
      .head = {.type = FIOBJ_T_NUMBER, .ref = ((~(uint32_t)0) >> 4)},
      .i = num,
  };
  return (FIOBJ)ret;
}

/* *****************************************************************************
Float API
***************************************************************************** */

/** Creates a Float object. Remember to use `fiobj_free`.  */
FIOBJ fiobj_float_new(double num) {
  fiobj_float_s *o = fio_malloc(sizeof(*o));
  if (!o) {
    perror("ERROR: fiobj float couldn't allocate memory");
    exit(errno);
  }
  *o = (fiobj_float_s){
      .head =
          {
              .type = FIOBJ_T_FLOAT,
              .ref = 1,
          },
      .f = num,
  };
  return (FIOBJ)o;
}

/** Mutates a Float object's value. Effects every object's reference!  */
void fiobj_float_set(FIOBJ obj, double num) {
  assert(FIOBJ_TYPE_IS(obj, FIOBJ_T_FLOAT));
  obj2float(obj)->f = num;
}

static pthread_key_t float_ret_key;
static pthread_once_t float_ret_once = PTHREAD_ONCE_INIT;
static void init_float_ret_key(void) {
  pthread_key_create(&float_ret_key, free);
}
static void init_float_ret_ptr(void) {
  fiobj_float_s *ret = malloc(sizeof(fiobj_float_s));
  FIO_ASSERT_ALLOC(ret);
  memset(ret, 0, sizeof(fiobj_float_s));
  pthread_setspecific(float_ret_key, ret);
}
/** Creates a temporary Number object. This ignores `fiobj_free`. */
FIOBJ fiobj_float_tmp(double num) {
  pthread_once(&float_ret_once, init_float_ret_key);
  fiobj_float_s *ret = pthread_getspecific(float_ret_key);
  if (!ret) {
    init_float_ret_ptr();
    ret = pthread_getspecific(float_ret_key);
  }
  *ret = (fiobj_float_s){
      .head =
          {
              .type = FIOBJ_T_FLOAT,
              .ref = ((~(uint32_t)0) >> 4),
          },
      .f = num,
  };
  return (FIOBJ)ret;
}

/* *****************************************************************************
Numbers to Strings - Buffered
***************************************************************************** */

static pthread_key_t num_str_buffer_key;
static pthread_once_t num_str_buffer_once = PTHREAD_ONCE_INIT;
static void init_num_str_buffer_key(void) {
  pthread_key_create(&num_str_buffer_key, free);
}
static void init_num_str_buffer_ptr(void) {
  char *num_str_buffer = malloc(sizeof(char)*512);
  FIO_ASSERT_ALLOC(num_str_buffer);
  memset(num_str_buffer, 0, sizeof(char)*512);
  pthread_setspecific(num_str_buffer_key, num_str_buffer);
}

fio_str_info_s fio_ltocstr(long i) {
  pthread_once(&num_str_buffer_once, init_num_str_buffer_key);
  char *num_buffer = pthread_getspecific(num_str_buffer_key);
  if (!num_buffer) {
    init_num_str_buffer_ptr();
    num_buffer = pthread_getspecific(num_str_buffer_key);
  }
  return (fio_str_info_s){.data = num_buffer,
                          .len = fio_ltoa(num_buffer, i, 10)};
}
fio_str_info_s fio_ftocstr(double f) {
  pthread_once(&num_str_buffer_once, init_num_str_buffer_key);
  char *num_buffer = pthread_getspecific(num_str_buffer_key);
  if (!num_buffer) {
    init_num_str_buffer_ptr();
    num_buffer = pthread_getspecific(num_str_buffer_key);
  }
  return (fio_str_info_s){.data = num_buffer,
                          .len = fio_ftoa(num_buffer, f, 10)};
}

/* *****************************************************************************
Tests
***************************************************************************** */

#if DEBUG
void fiobj_test_numbers(void) {
#define NUMTEST_ASSERT(cond, ...)                                              \
  if (!(cond)) {                                                               \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "Testing failed.\n");                                      \
    exit(-1);                                                                  \
  }
  FIOBJ i = fiobj_num_new(8);
  fprintf(stderr, "=== Testing Numbers\n");
  fprintf(stderr, "* FIOBJ_NUMBER_SIGN_MASK == %p\n",
          (void *)FIOBJ_NUMBER_SIGN_MASK);
  fprintf(stderr, "* FIOBJ_NUMBER_SIGN_BIT == %p\n",
          (void *)FIOBJ_NUMBER_SIGN_BIT);
  fprintf(stderr, "* FIOBJ_NUMBER_SIGN_EXCLUDE_BIT == %p\n",
          (void *)FIOBJ_NUMBER_SIGN_EXCLUDE_BIT);
  NUMTEST_ASSERT(FIOBJ_TYPE_IS(i, FIOBJ_T_NUMBER),
                 "* FIOBJ_TYPE_IS failed to return true.");
  NUMTEST_ASSERT((FIOBJ_TYPE(i) == FIOBJ_T_NUMBER),
                 "* FIOBJ_TYPE failed to return type.");
  NUMTEST_ASSERT(!FIOBJ_TYPE_IS(i, FIOBJ_T_NULL),
                 "* FIOBJ_TYPE_IS failed to return false.");
  NUMTEST_ASSERT((i & FIOBJECT_NUMBER_FLAG),
                 "* Number 8 was dynamically allocated?! %p\n", (void *)i);
  NUMTEST_ASSERT((fiobj_obj2num(i) == 8), "* Number 8 was not returned! %p\n",
                 (void *)i);
  fiobj_free(i);
  i = fiobj_num_new(-1);
  NUMTEST_ASSERT((i & FIOBJECT_NUMBER_FLAG),
                 "* Number -1 was dynamically allocated?! %p\n", (void *)i);
  NUMTEST_ASSERT((fiobj_obj2num(i) == -1), "* Number -1 was not returned! %p\n",
                 (void *)i);
  fiobj_free(i);
  i = fiobj_num_new(INTPTR_MAX);
  NUMTEST_ASSERT((i & FIOBJECT_NUMBER_FLAG) == 0,
                 "* INTPTR_MAX was statically allocated?! %p\n", (void *)i);
  NUMTEST_ASSERT((fiobj_obj2num(i) == INTPTR_MAX),
                 "* INTPTR_MAX was not returned! %p\n", (void *)i);
  NUMTEST_ASSERT(
      FIOBJ_TYPE_IS(i, FIOBJ_T_NUMBER),
      "* FIOBJ_TYPE_IS failed to return true for dynamic allocation.");
  NUMTEST_ASSERT((FIOBJ_TYPE(i) == FIOBJ_T_NUMBER),
                 "* FIOBJ_TYPE failed to return type for dynamic allocation.");
  fiobj_free(i);
  i = fiobj_num_new(INTPTR_MIN);
  NUMTEST_ASSERT((i & FIOBJECT_NUMBER_FLAG) == 0,
                 "* INTPTR_MIN was statically allocated?! %p\n", (void *)i);
  NUMTEST_ASSERT((fiobj_obj2num(i) == INTPTR_MIN),
                 "* INTPTR_MIN was not returned! %p\n", (void *)i);
  fiobj_free(i);
  fprintf(stderr, "* passed.\n");
  fprintf(stderr, "=== Testing Floats\n");
  i = fiobj_float_new(1.0);
  NUMTEST_ASSERT(((i & FIOBJECT_NUMBER_FLAG) == 0),
                 "* float 1 was statically allocated?! %p\n", (void *)i);
  NUMTEST_ASSERT((fiobj_obj2float(i) == 1.0),
                 "* Float 1.0 was not returned! %p\n", (void *)i);
  fiobj_free(i);
  i = fiobj_float_new(-1.0);
  NUMTEST_ASSERT((i & FIOBJECT_NUMBER_FLAG) == 0,
                 "* Float -1 was statically allocated?! %p\n", (void *)i);
  NUMTEST_ASSERT((fiobj_obj2float(i) == -1.0),
                 "* Float -1 was not returned! %p\n", (void *)i);
  fiobj_free(i);
  fprintf(stderr, "* passed.\n");
}
#endif
