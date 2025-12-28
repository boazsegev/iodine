#ifndef H___IODINE_JSON___H
#define H___IODINE_JSON___H
#include "iodine.h"

/* *****************************************************************************
Iodine JSON - Fast JSON Parsing and Stringification

This module provides the Iodine::JSON Ruby module for JSON operations.
It's primarily used internally for WebSocket message serialization but
is also exposed as a public API.

Features:
- Fast JSON stringification (Ruby objects to JSON strings)
- JSON parsing (JSON strings to Ruby objects)
- Beautified/pretty-printed JSON output
- Handles nested arrays, hashes, strings, numbers, booleans, nil

Performance Notes:
- Stringification is fast (single memory copy from C to Ruby)
- Parsing is slower than alternatives (double copy: JSON->FIOBJ->Ruby)
- For production JSON work, consider using the 'oj' gem instead

Ruby API (Iodine::JSON):
- Iodine::JSON.parse(json_string)     - Parse JSON to Ruby objects
- Iodine::JSON.stringify(object)      - Convert Ruby object to JSON
- Iodine::JSON.dump(object)           - Alias for stringify
- Iodine::JSON.beautify(object)       - Pretty-printed JSON output
- Iodine::JSON.beautify_slow(object)  - Alternative beautifier via FIOBJ
- Iodine::JSON.parse_slow(json)       - Alternative parser via FIOBJ
***************************************************************************** */

/* *****************************************************************************
JSON Stringifier - Ruby to JSON String Conversion
***************************************************************************** */

static char *iodine_json_stringify_key(char *dest, VALUE tmp) {
  /* Note: keys MUST be Strings  */
  switch (rb_type(tmp)) {
  case RUBY_T_SYMBOL: tmp = rb_sym2str(tmp); /* fall through */
  case RUBY_T_STRING:
  string_key:
    dest = fio_bstr_write(dest, "\"", 1);
    dest =
        fio_bstr_write_escape(dest, RSTRING_PTR(tmp), (size_t)RSTRING_LEN(tmp));
    dest = fio_bstr_write(dest, "\"", 1);
    break;
  case RUBY_T_FIXNUM:
    dest = fio_bstr_write2(dest,
                           FIO_STRING_WRITE_STR2("\"", 1),
                           FIO_STRING_WRITE_NUM(NUM2LL(tmp)),
                           FIO_STRING_WRITE_STR2("\"", 1));
    break;
  case RUBY_T_TRUE: dest = fio_bstr_write(dest, "\"true\"", 6); break;
  case RUBY_T_FALSE: dest = fio_bstr_write(dest, "\"false\"", 7); break;
  case RUBY_T_NIL: dest = fio_bstr_write(dest, "\"null\"", 6); break;
  default:
    tmp = rb_any_to_s(tmp);
    if (RB_TYPE_P(tmp, RUBY_T_STRING))
      goto string_key;
    dest = fio_bstr_write(dest, "\"error\"", 7);
  }
  return dest;
}

static char *iodine_json_stringify2bstr(char *dest, VALUE o);

static int iodine_json_hash_each_callback(VALUE k, VALUE v, VALUE dest_) {
  char **pdest = (char **)dest_;
  *pdest = iodine_json_stringify_key(*pdest, k);
  *pdest = fio_bstr_write(*pdest, ":", 1);
  *pdest = iodine_json_stringify2bstr(*pdest, v);
  *pdest = fio_bstr_write(*pdest, ",", 1);
  return ST_CONTINUE;
}

/* Converts a VALUE to a fio_bstr */
static char *iodine_json_stringify2bstr(char *dest, VALUE o) {
  size_t tmp;
  switch (rb_type(o)) {
  case RUBY_T_NIL: return (dest = fio_bstr_write(dest, "null", 4));
  case RUBY_T_TRUE: return (dest = fio_bstr_write(dest, "true", 4));
  case RUBY_T_FALSE: return (dest = fio_bstr_write(dest, "false", 5));
  case RUBY_T_ARRAY:
    tmp = rb_array_len(o);
    if (!tmp) {
      dest = fio_bstr_write(dest, "[]", 2);
      return dest;
    }
    dest = fio_bstr_write(dest, "[", 1);
    for (size_t i = 0; i < tmp; ++i) {
      dest = iodine_json_stringify2bstr(dest, RARRAY_PTR(o)[i]);
      dest = fio_bstr_write(dest, ",", 1);
    }
    dest[fio_bstr_len(dest) - 1] = ']';
    return dest;
  case RUBY_T_HASH:
    tmp = rb_hash_size_num(o);
    if (!tmp) {
      dest = fio_bstr_write(dest, "{}", 2);
      return dest;
    }
    dest = fio_bstr_write(dest, "{", 1);
    rb_hash_foreach(o, iodine_json_hash_each_callback, (VALUE)(&dest));
    dest[fio_bstr_len(dest) - 1] = '}';
    return dest;
  case RUBY_T_FIXNUM: return (dest = fio_bstr_write_i(dest, RB_NUM2LL(o)));
  case RUBY_T_FLOAT: {
    FIO_STR_INFO_TMP_VAR(buf, 232);
    buf.len = fio_ftoa(buf.buf, RFLOAT_VALUE(o), 10);
    dest = fio_bstr_write(dest, buf.buf, buf.len);
    return dest;
  }
  case RUBY_T_SYMBOL: o = rb_sym2str(o); /* fall through */
  case RUBY_T_STRING:
  add_string:
    dest = fio_bstr_write(dest, "\"", 1);
    dest = fio_bstr_write_escape(dest, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    dest = fio_bstr_write(dest, "\"", 1);
    return dest;
  default:
    if (1) {
      VALUE tmp = rb_check_funcall(o, IODINE_TO_JSON_ID, 0, NULL);
      if (tmp != RUBY_Qundef && RB_TYPE_P(tmp, RUBY_T_STRING))
        return (dest = fio_bstr_write(dest,
                                      RSTRING_PTR(tmp),
                                      (size_t)RSTRING_LEN(tmp)));
      o = rb_funcallv(o, IODINE_TO_S_ID, 0, NULL);
    }
    if (!RB_TYPE_P(o, RUBY_T_STRING)) {
      FIO_LOG_ERROR("Iodine::JSON.stringify called with an object that doesn't "
                    "respond to #to_s.");
      return (dest = fio_bstr_write(dest, "null", 4));
    }
    goto add_string;
  }
}

/* *****************************************************************************
JSON Beautifier - Pretty-Printed JSON Output
***************************************************************************** */

typedef struct {
  char *o;
  size_t t;
} iodine_json_beautify2bstr_s;

static char *iodine_json_beautify2bstr(iodine_json_beautify2bstr_s *dest,
                                       VALUE o);

FIO_IFUNC void iodine_json_beautify2bstr_pad(iodine_json_beautify2bstr_s *d) {
  if (!d->t)
    return;
  size_t len = fio_bstr_len(d->o);
  d->o = fio_bstr_len_set(d->o, len + d->t + 1);
  char *tmp = d->o + len;
  *tmp++ = '\n';
  FIO_MEMSET(tmp, '\t', d->t);
}

static int iodine_json_hash_each_beautify_callback(VALUE k,
                                                   VALUE v,
                                                   VALUE dest_) {
  iodine_json_beautify2bstr_s *d = (iodine_json_beautify2bstr_s *)dest_;
  iodine_json_beautify2bstr_pad(d);
  d->o = iodine_json_stringify_key(d->o, k);
  d->o = fio_bstr_write(d->o, ": ", 2);
  iodine_json_beautify2bstr(d, v);
  d->o = fio_bstr_write(d->o, ",", 1);
  return ST_CONTINUE;
}

/* Converts a VALUE to a fio_bstr */
static char *iodine_json_beautify2bstr(iodine_json_beautify2bstr_s *d,
                                       VALUE o) {
  size_t tmp;
  switch (rb_type(o)) {
  case RUBY_T_NIL: return (d->o = fio_bstr_write(d->o, "null", 4));
  case RUBY_T_TRUE: return (d->o = fio_bstr_write(d->o, "true", 4));
  case RUBY_T_FALSE: return (d->o = fio_bstr_write(d->o, "false", 5));
  case RUBY_T_ARRAY:
    tmp = rb_array_len(o);
    if (!tmp) {
      d->o = fio_bstr_write(d->o, "[]", 2);
      return d->o;
    }
    d->o = fio_bstr_write(d->o, "[", 1);
    ++d->t;
    for (size_t i = 0; i < tmp; ++i) {
      iodine_json_beautify2bstr_pad(d);
      iodine_json_beautify2bstr(d, RARRAY_PTR(o)[i]);
      d->o = fio_bstr_write(d->o, ",", 1);
    }
    d->o[fio_bstr_len(d->o) - 1] = '\n';
    --d->t;
    iodine_json_beautify2bstr_pad(d);
    d->o = fio_bstr_write(d->o, "]", 1);
    return d->o;
  case RUBY_T_HASH:
    tmp = rb_hash_size_num(o);
    if (!tmp) {
      d->o = fio_bstr_write(d->o, "{}", 2);
      return d->o;
    }
    d->o = fio_bstr_write(d->o, "{", 1);
    ++d->t;
    rb_hash_foreach(o, iodine_json_hash_each_beautify_callback, (VALUE)(d));
    d->o[fio_bstr_len(d->o) - 1] = '\n';
    --d->t;
    iodine_json_beautify2bstr_pad(d);
    d->o = fio_bstr_write(d->o, "}", 1);
    return d->o;
  case RUBY_T_FIXNUM: return (d->o = fio_bstr_write_i(d->o, RB_NUM2LL(o)));
  case RUBY_T_FLOAT: {
    FIO_STR_INFO_TMP_VAR(buf, 232);
    buf.len = fio_ftoa(buf.buf, RFLOAT_VALUE(o), 10);
    d->o = fio_bstr_write(d->o, buf.buf, buf.len);
    return d->o;
  }
  case RUBY_T_SYMBOL: o = rb_sym2str(o); /* fall through */
  case RUBY_T_STRING:
  add_string:
    d->o = fio_bstr_write(d->o, "\"", 1);
    d->o = fio_bstr_write_escape(d->o, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    d->o = fio_bstr_write(d->o, "\"", 1);
    return d->o;
  default:
    if (1) {
      VALUE tmp = rb_check_funcall(o, IODINE_TO_JSON_ID, 0, NULL);
      if (tmp != RUBY_Qundef && RB_TYPE_P(tmp, RUBY_T_STRING))
        return (d->o = fio_bstr_write(d->o,
                                      RSTRING_PTR(tmp),
                                      (size_t)RSTRING_LEN(tmp)));
      o = rb_funcallv(o, IODINE_TO_S_ID, 0, NULL);
      if (RB_TYPE_P(o, RUBY_T_STRING))
        goto add_string;
    }
    FIO_LOG_ERROR("Iodine::JSON.stringify called with an object that doesn't "
                  "respond to neither #to_json nor #to_s.");
    return (d->o = fio_bstr_write(d->o, "null", 4));
  }
}

/* *****************************************************************************
FIOBJ => Ruby Bridge - Convert facil.io Objects to Ruby
***************************************************************************** */

typedef struct iodine_fiobj2ruby_task_s {
  VALUE out;
} iodine_fiobj2ruby_task_s;

static VALUE iodine_fiobj2ruby(FIOBJ o);

static int iodine_fiobj2ruby_array_task(fiobj_array_each_s *e) {
  rb_ary_push((VALUE)e->udata, iodine_fiobj2ruby(e->value));
  return 0;
}
static int iodine_fiobj2ruby_hash_task(fiobj_hash_each_s *e) {
  VALUE k = iodine_fiobj2ruby(e->key);
  VALUE v = iodine_fiobj2ruby(e->value);
  rb_hash_aset((VALUE)e->udata, k, v);
  return 0;
}

/**
 * Converts a FIOBJ (facil.io object) to a Ruby VALUE.
 *
 * Recursively converts FIOBJ types to their Ruby equivalents:
 * - FIOBJ_T_TRUE/FALSE -> Qtrue/Qfalse
 * - FIOBJ_T_NUMBER -> Fixnum/Bignum
 * - FIOBJ_T_FLOAT -> Float
 * - FIOBJ_T_STRING -> String
 * - FIOBJ_T_ARRAY -> Array
 * - FIOBJ_T_HASH -> Hash
 * - FIOBJ_T_NULL/INVALID -> Qnil
 *
 * @param o The FIOBJ to convert
 * @return Ruby VALUE equivalent
 *
 * @note Does NOT place VALUE in STORE automatically.
 */
static VALUE iodine_fiobj2ruby(FIOBJ o) {
  VALUE r;
  switch (FIOBJ_TYPE(o)) {
  case FIOBJ_T_TRUE: return Qtrue;
  case FIOBJ_T_FALSE: return Qfalse;
  case FIOBJ_T_NUMBER: return RB_LL2NUM(fiobj_num2i(o));
  case FIOBJ_T_FLOAT: return rb_float_new(fiobj_float2f(o));
  case FIOBJ_T_STRING: return rb_str_new(fiobj_str_ptr(o), fiobj_str_len(o));
  case FIOBJ_T_ARRAY:
    STORE.gc_stop();
    r = rb_ary_new_capa(fiobj_array_count(o));
    fiobj_array_each(o, iodine_fiobj2ruby_array_task, (void *)r, 0);
    STORE.gc_start();
    return r;
  case FIOBJ_T_HASH:
    STORE.gc_stop();
    r = rb_hash_new();
    fiobj_hash_each(o, iodine_fiobj2ruby_hash_task, (void *)r, 0);
    STORE.gc_start();
    return r;
  // case FIOBJ_T_NULL: /* fall through */
  // case FIOBJ_T_INVALID: /* fall through */
  default: return Qnil;
  }
}

/* *****************************************************************************
Ruby => FIOBJ Bridge - Convert Ruby Objects to facil.io Objects
***************************************************************************** */

typedef struct iodine_ruby2fiobj_task_s {
  FIOBJ out;
} iodine_ruby2fiobj_task_s;

static FIOBJ iodine_ruby2fiobj(VALUE o);

static int iodine_ruby2fiobj_hash_each_task(VALUE n, VALUE v, VALUE h_) {
  FIOBJ h = (FIOBJ)h_;
  FIOBJ key = iodine_ruby2fiobj(n);
  fiobj_hash_set(h, key, iodine_ruby2fiobj(v), NULL);
  fiobj_free(key); /* by default, keys aren't owned by the hash */
  return ST_CONTINUE;
}

/**
 * Converts a Ruby VALUE to a FIOBJ (facil.io object).
 *
 * Recursively converts Ruby types to their FIOBJ equivalents:
 * - Qtrue/Qfalse -> fiobj_true()/fiobj_false()
 * - Fixnum -> fiobj_num_new()
 * - Float -> fiobj_float_new()
 * - Symbol/String -> fiobj_str_new_cstr()
 * - Array -> fiobj_array_new()
 * - Hash -> fiobj_hash_new()
 * - nil/undef -> fiobj_null()
 * - Other -> calls #to_json or #to_s
 *
 * @param o The Ruby VALUE to convert
 * @return FIOBJ equivalent
 *
 * @note Does NOT place VALUE in STORE automatically.
 */
static FIOBJ iodine_ruby2fiobj(VALUE o) {
  FIOBJ r;
  VALUE tmp;
  switch (rb_type(o)) {
  case RUBY_T_TRUE: return fiobj_true();
  case RUBY_T_FALSE: return fiobj_false();
  case RUBY_T_FIXNUM: return fiobj_num_new((intptr_t)RB_NUM2LL(o));
  case RUBY_T_FLOAT: return fiobj_float_new(rb_float_value(o));
  case RUBY_T_SYMBOL: o = rb_sym_to_s(o); /* fall through */
  case RUBY_T_STRING: return fiobj_str_new_cstr(RSTRING_PTR(o), RSTRING_LEN(o));
  case RUBY_T_ARRAY:
    if (1) {
      const size_t alen = rb_array_len(o);
      r = fiobj_array_new();
      if (alen)
        fiobj_array_reserve(r, (int64_t)alen);
      for (size_t i = 0; i < alen; ++i)
        fiobj_array_push(r, iodine_ruby2fiobj(RARRAY_PTR(o)[i]));
      return r;
    }
  case RUBY_T_HASH:
    r = fiobj_hash_new();
    if (rb_hash_size_num(o))
      fiobj_hash_reserve(r, rb_hash_size_num(o));
    rb_hash_foreach(o, iodine_ruby2fiobj_hash_each_task, (VALUE)r);
    return r;
  case RUBY_T_NIL:   /* fall through */
  case RUBY_T_UNDEF: /* fall through */
  case RUBY_T_NONE: return fiobj_null();
  default:
    tmp = rb_check_funcall(o, IODINE_TO_JSON_ID, 0, NULL);
    if (tmp != RUBY_Qundef && RB_TYPE_P(tmp, RUBY_T_STRING)) {
      size_t c = 0;
      r = fiobj_json_parse((fio_str_info_s)IODINE_RSTR_INFO(tmp), &c);
      return r;
    }
    o = rb_any_to_s(o);
    if (RB_TYPE_P(o, RUBY_T_STRING))
      return fiobj_str_new_cstr(RSTRING_PTR(o), RSTRING_LEN(o));
    return fiobj_null();
  }
}

/* *****************************************************************************
JSON Parser (Indirect) - Parse via FIOBJ intermediate
***************************************************************************** */

/**
 * Parses a JSON string to Ruby objects via FIOBJ intermediate.
 *
 * This is the slower parsing path that:
 * 1. Parses JSON string to FIOBJ tree
 * 2. Converts FIOBJ tree to Ruby objects
 *
 * @param self The Iodine::JSON module
 * @param rstr The JSON string to parse (String)
 * @return Ruby object tree (Hash, Array, String, etc.)
 *
 * Ruby: Iodine::JSON.parse_slow(json_string)
 */
static VALUE iodine_json_parse_indirect(VALUE self, VALUE rstr) {
  VALUE r = Qnil;
  rb_check_type(rstr, RUBY_T_STRING);
  size_t consumed = 0;
  FIOBJ tmp =
      fiobj_json_parse((fio_str_info_s)IODINE_RSTR_INFO(rstr), &consumed);
  STORE.gc_stop();
  r = iodine_fiobj2ruby(tmp);
  STORE.gc_start();
  fiobj_free(tmp);
  return r;
}

/* *****************************************************************************
JSON Parser (Direct) - Parse Directly to Ruby Objects

This parser uses facil.io's streaming JSON parser with callbacks that
create Ruby objects directly, avoiding the FIOBJ intermediate step.
This is the faster parsing path used by Iodine::JSON.parse().
***************************************************************************** */

/** JSON parser callback implementations for direct Ruby object creation */
/** NULL object was detected. Returns new object as `void *`. */
FIO_SFUNC void *iodine___json_on_null(void) { return (void *)Qnil; }
/** TRUE object was detected. Returns new object as `void *`. */
FIO_SFUNC void *iodine___json_on_true(void) { return (void *)Qtrue; }
/** FALSE object was detected. Returns new object as `void *`. */
FIO_SFUNC void *iodine___json_on_false(void) { return (void *)Qfalse; }
/** Number was detected (long long). Returns new object as `void *`. */
FIO_SFUNC void *iodine___json_on_number(int64_t i) {
  return (void *)(LL2NUM(((long long)i)));
}
/** Float was detected (double).Returns new object as `void *`.  */
FIO_SFUNC void *iodine___json_on_float(double f) { return (void *)DBL2NUM(f); }
/** String was detected (int / float). update `pos` to point at ending */
FIO_SFUNC void *iodine___json_on_string(const void *start, size_t len) {
  VALUE str;
  if (len < 4096) {
    FIO_STR_INFO_TMP_VAR(buf, 4096);
    fio_string_write_unescape(&buf, NULL, start, len);
    str = rb_str_new(buf.buf, buf.len);
  } else {
    char *tmp = fio_bstr_write_unescape(NULL, start, len);
    str = rb_str_new(tmp, fio_bstr_len(tmp));
    fio_bstr_free(tmp);
  }
  return (void *)str;
}
FIO_SFUNC void *iodine___json_on_string_simple(const void *start, size_t len) {
  VALUE str = rb_str_new((const char *)start, len);
  return (void *)str;
}
/** Dictionary was detected. Returns ctx to hash map or NULL on error. */
FIO_SFUNC void *iodine___json_on_map(void *ctx, void *at) {
  (void)ctx, (void)at;
  VALUE map = rb_hash_new();
  return (void *)map;
}
/** Array was detected. Returns ctx to array or NULL on error. */
FIO_SFUNC void *iodine___json_on_array(void *ctx, void *at) {
  (void)ctx, (void)at;
  VALUE ary = rb_ary_new();
  return (void *)ary;
}
/** Array was detected. Returns non-zero on error. */
FIO_SFUNC int iodine___json_map_push(void *ctx, void *key, void *val) {
  rb_hash_aset((VALUE)ctx, (VALUE)key, (VALUE)val);
  return 0;
}
/** Array was detected. Returns non-zero on error. */
FIO_SFUNC int iodine___json_array_push(void *ctx, void *val) {
  rb_ary_push((VALUE)ctx, (VALUE)val);
  return 0;
}
/** Called for the `key` element in case of error or NULL value. */
FIO_SFUNC void iodine___json_free_unused_object(void *ctx) {}
/** the JSON parsing encountered an error - what to do with ctx? */
FIO_SFUNC void *iodine___json_on_error(void *ctx) { return (void *)Qnil; }

static fio_json_parser_callbacks_s IODINE_JSON_PARSER_CALLBACKS = {
    .on_null = iodine___json_on_null,
    .on_true = iodine___json_on_true,
    .on_false = iodine___json_on_false,
    .on_number = iodine___json_on_number,
    .on_float = iodine___json_on_float,
    .on_string = iodine___json_on_string,
    .on_string_simple = iodine___json_on_string_simple,
    .on_map = iodine___json_on_map,
    .on_array = iodine___json_on_array,
    .map_push = iodine___json_map_push,
    .array_push = iodine___json_array_push,
    .free_unused_object = iodine___json_free_unused_object,
    .on_error = iodine___json_on_error,
};

/* *****************************************************************************
API - Public Ruby Methods
***************************************************************************** */

/**
 * Parses a JSON string to Ruby objects (fast path).
 *
 * Uses the direct parser that creates Ruby objects without
 * an intermediate FIOBJ representation.
 *
 * @param self The Iodine::JSON module
 * @param rstr The JSON string to parse (String)
 * @return Ruby object tree (Hash, Array, String, etc.)
 *
 * Ruby: Iodine::JSON.parse(json_string)
 */
static VALUE iodine_json_parse(VALUE self, VALUE rstr) {
  VALUE r = Qnil;
  rb_check_type(rstr, RUBY_T_STRING);
  STORE.gc_stop();
  fio_json_result_s result = fio_json_parse(&IODINE_JSON_PARSER_CALLBACKS,
                                            RSTRING_PTR(rstr),
                                            RSTRING_LEN(rstr));
  STORE.gc_start();
  r = (VALUE)(result.ctx);
  STORE.hold(r);
  rb_str_buf_new(1); /* force GC to run if Ruby needs it */
  STORE.release(r);
  return r;
}

/**
 * Converts a Ruby object to a JSON string.
 *
 * Handles all standard Ruby types (Hash, Array, String, Numeric,
 * true, false, nil). For other objects, calls #to_json or #to_s.
 *
 * @param self The Iodine::JSON module
 * @param object The Ruby object to stringify
 * @return JSON string representation
 *
 * Ruby: Iodine::JSON.stringify(object)
 *       Iodine::JSON.dump(object)  # alias
 */
static VALUE iodine_json_stringify(VALUE self, VALUE object) {
  VALUE r = Qnil;
  char *str = fio_bstr_reserve(NULL, ((size_t)1 << 12) - 64);
  str = iodine_json_stringify2bstr(str, object);
  r = rb_str_new(str, (long)fio_bstr_len(str));
  fio_bstr_free(str);
  return r;
}

/**
 * Converts a Ruby object to a pretty-printed JSON string (slow path).
 *
 * Uses FIOBJ intermediate for formatting. Slower but produces
 * nicely indented output.
 *
 * @param self The Iodine::JSON module
 * @param object The Ruby object to stringify
 * @return Pretty-printed JSON string
 *
 * Ruby: Iodine::JSON.beautify_slow(object)
 */
static VALUE iodine_json_pretty(VALUE self, VALUE object) {
  VALUE r = Qnil;
  FIOBJ o = iodine_ruby2fiobj(object);
  FIOBJ out = fiobj2json(FIOBJ_T_INVALID, o, 1);
  fiobj_free(o);
  r = rb_str_new(fiobj_str_ptr(out), (long)fiobj_str_len(out));
  fiobj_free(out);
  return r;
}

/**
 * Converts a Ruby object to a pretty-printed JSON string (fast path).
 *
 * Uses direct stringification with indentation. Faster than
 * beautify_slow but produces similar output.
 *
 * @param self The Iodine::JSON module
 * @param object The Ruby object to stringify
 * @return Pretty-printed JSON string with newlines and tabs
 *
 * Ruby: Iodine::JSON.beautify(object)
 */
static VALUE iodine_json_beautify(VALUE self, VALUE object) {
  VALUE r = Qnil;
  iodine_json_beautify2bstr_s dest = {
      .o = fio_bstr_reserve(NULL, ((size_t)1 << 12) - 64),
  };
  iodine_json_beautify2bstr(&dest, object);
  r = rb_str_new(dest.o, (long)fio_bstr_len(dest.o));
  fio_bstr_free(dest.o);
  return r;
}

/** Initialize Iodine::JSON */ // clang-format off
/**
Iodine::JSON exposes the {Iodine::Connection#write} fallback behavior when called with non-String objects.

The fallback behavior is similar to (though faster than) calling:

        client.write(Iodine::JSON.stringify(data))

If you want to work with JSON, consider using the `oj` gem.

This API is mostly to test for Iodine JSON input/output errors and reflects what the C layer sees.

## Performance

Performance... could be better.

Converting Ruby objects into a JSON String (Strinfigying) should be fast even though the String data is copied twice, once to C and then to Ruby.

However, Converting a JSON object into Ruby Objects is currently slow and it is better to use the `oj` gem or even the Ruby builtin parser.

The reason is simple - the implementation is designed to create C objects (C Hash Maps, C Arrays, etc'), not Ruby objects. When converting from a String to Ruby Objevts, the data is copied twice, once to C and then to Ruby.

This especially effects parsing, where more objects are allocated,
whereas {Iodine::JSON.stringify} only (re)copies the String data which is a single continuous block of memory.

That's why {Iodine::JSON.stringify} is significantly faster than the Ruby `object.to_json` approach,
yet slower than `JSON.parse(json_string)`.

          require 'oj' rescue nil
          require 'benchmark/ips'
          require 'json'
          require 'iodine'

          def benchmark_json
            # make a big data store with nothings
            data_1000 = []
            1000.times do
              tmp = {f: rand() };
              tmp[:i] = (tmp[:f] * 1000000).to_i
              tmp[:str] = tmp[:i].to_s
              tmp[:sym] = tmp[:str].to_sym
              tmp[:ary] = []
              tmp[:ary_empty] = []
              tmp[:hash_empty] = Hash.new
              100.times {|i| tmp[:ary] << i }
              data_1000 << tmp
            end
            3.times do
              json_string = data_1000.to_json
              puts "-----"
              puts "Benchmark #{data_1000.length} item tree, and #{json_string.length} bytes of JSON"
              # benchmark stringification
              Benchmark.ips do |x|
                x.report("      Ruby obj.to_json") do |times|
                  data_1000.to_json
                end
                x.report("Iodine::JSON.stringify") do |times|
                  Iodine::JSON.stringify(data_1000)
                end
                if(defined?(Oj))
                  x.report("               Oj.dump") do |times|
                    Oj.dump(data_1000)
                  end
                end
                x.compare!
              end ; nil
              # benchmark parsing
              Benchmark.ips do |x|
                x.report("   Ruby JSON.parse") do |times|
                  JSON.parse(json_string)
                end
                x.report("Iodine::JSON.parse") do |times|
                  Iodine::JSON.parse(json_string)
                end
                if(defined?(Oj))
                  x.report("           Oj.load") do |times|
                    Oj.load(json_string)
                  end
                end
                x.compare!
              end
              data_1000 = data_1000.slice(0, (data_1000.length / 10))
              nil
            end
          end

          benchmark_json

*/
static void Init_Iodine_JSON(void) {
  VALUE m = rb_define_module_under(iodine_rb_IODINE, "JSON"); // clang-format on
  rb_define_singleton_method(m, "parse", iodine_json_parse, 1);
  rb_define_singleton_method(m, "parse_slow", iodine_json_parse_indirect, 1);
  rb_define_singleton_method(m, "stringify", iodine_json_stringify, 1);
  rb_define_singleton_method(m, "beautify_slow", iodine_json_pretty, 1);
  rb_define_singleton_method(m, "beautify", iodine_json_beautify, 1);
  rb_define_singleton_method(m, "dump", iodine_json_stringify, 1);
}
#endif /* H___IODINE_JSON___H */
