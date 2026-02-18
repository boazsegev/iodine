#ifndef H___IODINE_ARG_HELPER___H
#define H___IODINE_ARG_HELPER___H
#include "iodine.h"

/* *****************************************************************************
Ruby Multi-Argument Helper

Reads and validates method arguments (either "splat" or Hash Map).

    iodine_rb2c_arg(argc, argv,
         IODINE_ARG_RB(var_name, id_or_0, "name1", 1),
         IODINE_ARG_STR(var_name, id_or_0, "name2", 0),
         IODINE_ARG_NUM(var_name, id_or_0, "name3", 0),
         IODINE_ARG_PROC(var_name, id_or_0, "name4", 0));

The `IODINE_ARG_XXX` macro arguments are as follows:

    IODINE_ARG_XXX(target_varriable, id_or_0, named_argument, required)

Example:

    fio_buf_info_s file_name = {0};
    fio_buf_info_s file_content = {0};
    VALUE on_yaml_block = Qnil;
    iodine_rb2c_arg(argc, argv,
         IODINE_ARG_STR(file_name, 0, "file", 0),
         IODINE_ARG_STR(file_content, 0, "data", 0),
         IODINE_ARG_PROC(on_yaml_block, 0, "on_yaml", 1));

For Ruby methods declared as:

    static VALUE my_method(int argc, VALUE *argv, VALUE klass);
    rb_define_singleton_method(klass, "method", my_method, -1);

***************************************************************************** */

/* *****************************************************************************
Ruby arguments to C arguments
***************************************************************************** */

typedef struct {
  int expected_type;
  union {
    VALUE *rb;
    fio_buf_info_s *buf;
    fio_str_info_s *str;
    int64_t *num;
    size_t *zu;
    int32_t *i32;
    int16_t *i16;
    int8_t *i8;
    uint64_t *u64;
    uint32_t *u32;
    uint16_t *u16;
    uint8_t *u8;
  };
  ID id;
  fio_buf_info_s name;
  int required;
} iodine_rb2c_arg_s;

#define IODINE_ARG_RB(t_var, id_, name_, required_)                            \
  {                                                                            \
    .expected_type = 0, .rb = &(t_var), .id = (id_),                           \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_BUF(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 1, .buf = &(t_var), .id = (id_),                          \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_STR(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 2, .str = &(t_var), .id = (id_),                          \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_NUM(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 3, .num = &(t_var), .id = (id_),                          \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_PROC(t_var, id_, name_, required_)                          \
  {                                                                            \
    .expected_type = 4, .rb = &(t_var), .id = (id_),                           \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_SIZE_T(t_var, id_, name_, required_)                        \
  {                                                                            \
    .expected_type = 5, .zu = &(t_var), .id = (id_),                           \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_I32(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 6, .i32 = &(t_var), .id = (id_),                          \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_I16(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 7, .i16 = &(t_var), .id = (id_),                          \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_I8(t_var, id_, name_, required_)                            \
  {                                                                            \
    .expected_type = 8, .i8 = &(t_var), .id = (id_),                           \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_BOOL(t_var, id_, name_, required_)                          \
  {                                                                            \
    .expected_type = 9, .u8 = &(t_var), .id = (id_),                           \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_U64(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 10, .u64 = &(t_var), .id = (id_),                         \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_U32(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 11, .u32 = &(t_var), .id = (id_),                         \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_U16(t_var, id_, name_, required_)                           \
  {                                                                            \
    .expected_type = 12, .u16 = &(t_var), .id = (id_),                         \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }
#define IODINE_ARG_U8(t_var, id_, name_, required_)                            \
  {                                                                            \
    .expected_type = 13, .u8 = &(t_var), .id = (id_),                          \
    .name = FIO_BUF_INFO1(((char *)name_)), .required = (required_)            \
  }

/** Reads and validates method arguments (either "splat" or Hash Map). */
static int iodine_rb2c_arg(int argc, const VALUE *argv, iodine_rb2c_arg_s *a) {
  int i = 0;
  VALUE tmp = Qnil;

#define IODINE_RB2C_STORE_ARG()                                                \
  if (!a[i].rb)                                                                \
    goto too_many_arguments;                                                   \
  if (tmp == Qnil && a[i].expected_type != 4) {                                \
    if (a[i].required)                                                         \
      goto missing_required;                                                   \
    continue;                                                                  \
  }                                                                            \
  switch (a[i].expected_type) {                                                \
  case 0: a[i].rb[0] = tmp; continue;                                          \
  case 1:                                                                      \
    if (RB_TYPE_P(tmp, RUBY_T_SYMBOL))                                         \
      tmp = rb_sym2str(tmp);                                                   \
    if (!RB_TYPE_P(tmp, RUBY_T_STRING))                                        \
      rb_raise(rb_eTypeError,                                                  \
               "%s should be a String (or Symbol)",                            \
               a[i].name.buf);                                                 \
    a[i].buf[0] = FIO_BUF_INFO2(RSTRING_PTR(tmp), (size_t)RSTRING_LEN(tmp));   \
    continue;                                                                  \
  case 2:                                                                      \
    if (RB_TYPE_P(tmp, RUBY_T_SYMBOL))                                         \
      tmp = rb_sym2str(tmp);                                                   \
    if (!RB_TYPE_P(tmp, RUBY_T_STRING))                                        \
      rb_raise(rb_eTypeError,                                                  \
               "%s should be a String (or Symbol)",                            \
               a[i].name.buf);                                                 \
    a[i].str[0] = FIO_STR_INFO2(RSTRING_PTR(tmp), (size_t)RSTRING_LEN(tmp));   \
    continue;                                                                  \
  case 3:                                                                      \
    if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                        \
      rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);         \
    a[i].num[0] = NUM2LL(tmp);                                                 \
    continue;                                                                  \
  case 4:                                                                      \
    if (tmp == Qnil) {                                                         \
      if (rb_block_given_p())                                                  \
        tmp = rb_block_proc();                                                 \
      else if (a[i].required)                                                  \
        goto missing_required;                                                 \
      else                                                                     \
        continue;                                                              \
    } else if (tmp != Qnil && !rb_respond_to(tmp, rb_intern2("call", 4)))      \
      rb_raise(rb_eArgError, "a callback object MUST respond to `call`");      \
    a[i].rb[0] = tmp;                                                          \
    continue;                                                                  \
  case 5:                                                                      \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      a[i].zu[0] = NUM2SIZET(tmp);                                             \
    }                                                                          \
    continue;                                                                  \
  case 6:                                                                      \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      if (((NUM2ULL(tmp) >> 32) + 1) > 1)                                      \
        rb_raise(rb_eRangeError, "%s out of range", a[i].name.buf);            \
      a[i].i32[0] = ((int32_t)NUM2INT(tmp));                                   \
    }                                                                          \
    continue;                                                                  \
  case 7:                                                                      \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      if (((NUM2ULL(tmp) >> 16) + 1) > 1)                                      \
        rb_raise(rb_eRangeError, "%s out of range", a[i].name.buf);            \
      a[i].i16[0] = ((int16_t)NUM2SHORT(tmp));                                 \
    }                                                                          \
    continue;                                                                  \
  case 8:                                                                      \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      if (((NUM2ULL(tmp) >> 8) + 1) > 1)                                       \
        rb_raise(rb_eRangeError, "%s out of range", a[i].name.buf);            \
      a[i].i8[0] = ((int8_t)NUM2CHR(tmp));                                     \
    }                                                                          \
    continue;                                                                  \
  case 9:                                                                      \
    if (tmp != Qnil) {                                                         \
      if (tmp != Qtrue && tmp != Qfalse)                                       \
        if (!RB_TYPE_P(tmp, RUBY_T_TRUE))                                      \
          rb_raise(rb_eTypeError, "%s should be a Boolean", a[i].name.buf);    \
      a[i].u8[0] = (tmp == Qtrue);                                             \
    }                                                                          \
    continue;                                                                  \
  case 10:                                                                     \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      if ((size_t)NUM2ULL(tmp) > 0xFFFFFFFFFFFFFFFFULL)                        \
        rb_raise(rb_eRangeError, "%s out of range", a[i].name.buf);            \
      a[i].u64[0] = (uint64_t)NUM2ULL(tmp);                                    \
    }                                                                          \
    continue;                                                                  \
  case 11:                                                                     \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      if ((size_t)NUM2ULL(tmp) > 0xFFFFFFFFULL)                                \
        rb_raise(rb_eRangeError, "%s out of range", a[i].name.buf);            \
      a[i].u32[0] = (uint32_t)NUM2ULL(tmp);                                    \
    }                                                                          \
    continue;                                                                  \
  case 12:                                                                     \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      if ((size_t)NUM2LL(tmp) > 0xFFFFULL)                                     \
        rb_raise(rb_eRangeError, "%s out of range", a[i].name.buf);            \
      a[i].u16[0] = (uint16_t)NUM2SHORT(tmp);                                  \
    }                                                                          \
    continue;                                                                  \
  case 13:                                                                     \
    if (tmp != Qnil) {                                                         \
      if (!RB_TYPE_P(tmp, RUBY_T_FIXNUM))                                      \
        rb_raise(rb_eTypeError, "%s should be a Number", a[i].name.buf);       \
      if ((size_t)NUM2LL(tmp) > 0xFFULL)                                       \
        rb_raise(rb_eRangeError, "%s out of range", a[i].name.buf);            \
      a[i].u8[0] = (uint8_t)NUM2CHR(tmp);                                      \
    }                                                                          \
    continue;                                                                  \
  default:                                                                     \
    rb_raise(rb_eException,                                                    \
             "C code failure - missing valid expected_type @ %s",              \
             a[i].name.buf);                                                   \
  }
  /* FIXME: allow partially named arguments. */
  if (argc) {
    --argc;
    for (; i < argc; ++i) {
      /* unnamed parameters (list of parameters), excluding last parameter */
      tmp = argv[i];
      IODINE_RB2C_STORE_ARG();
    }
    /* last parameter is special, as it may be a named parameter Hash */
    tmp = argv[argc];
    i = argc; /* does `for` add even after condition breaks loop? no, so why? */

    if (RB_TYPE_P(tmp, RUBY_T_HASH)) { /* named parameters (hash table) */
      VALUE tbl = tmp;
      if (a[i].expected_type == 0)
        a[i].rb[0] = tmp;
      for (; a[i].rb; ++i) {
        tmp = rb_hash_aref(
            tbl,
            rb_id2sym(a[i].id ? a[i].id
                              : rb_intern2(a[i].name.buf, a[i].name.len)));
        IODINE_RB2C_STORE_ARG();
      }
      return 0;
    }
    if (a[i].rb) {
      do {
        IODINE_RB2C_STORE_ARG();
      } while (0);
      ++i;
    }
    tmp = Qnil;
  }
  for (; a[i].rb; ++i) {
    /* possible leftover last parameter */
    if (a[i].expected_type == 4) {
      IODINE_RB2C_STORE_ARG();
      continue;
    }
    if (a[i].required)
      goto missing_required;
  }
  return 0;

#undef IODINE_RB2C_STORE_ARG
too_many_arguments:
  rb_raise(rb_eArgError, "too many arguments in method call.");
  return -1;
missing_required:
  rb_raise(rb_eArgError, "missing required argument %s.", a[i].name.buf);
  return -1;
}

/**
 * Reads and validates method arguments (either "splat" or Hash Map).
 *
 * Use:
 *
 *     iodine_rb2c_arg(argc, argv,
 *          IODINE_ARG_RB(var_name, id_or_0, "name1", 1),
 *          IODINE_ARG_STR(var_name, id_or_0, "name2", 0),
 *          IODINE_ARG_NUM(var_name, id_or_0, "name3", 0),
 *          IODINE_ARG_PROC(var_name, id_or_0, "name4", 0));
 */
#define iodine_rb2c_arg(argc, argv, ...)                                       \
  iodine_rb2c_arg(argc, argv, (iodine_rb2c_arg_s[]){__VA_ARGS__, {0}})

#endif /* H___IODINE_ARG_HELPER___H */
