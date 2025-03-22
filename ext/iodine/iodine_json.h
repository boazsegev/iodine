#ifndef H___IODINE_JSON___H
#define H___IODINE_JSON___H
#include "iodine.h"

/* *****************************************************************************
JSON Stringifier.
***************************************************************************** */
static char *iodine_json_stringify2bstr(char *dest, VALUE o);

static int iodine_json_hash_each_callback(VALUE k, VALUE v, VALUE dest_) {
  char **pdest = (char **)dest_;
  *pdest = iodine_json_stringify2bstr(*pdest, k);
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
  default:
    if (RB_TYPE_P(o, RUBY_T_SYMBOL))
      // o = rb_sym_to_s(o);
      o = rb_sym2str(o);
    if (!RB_TYPE_P(o, RUBY_T_STRING))
      o = rb_funcallv(o, IODINE_TO_S_ID, 0, NULL);
    if (!RB_TYPE_P(o, RUBY_T_STRING)) {
      FIO_LOG_ERROR("Iodine::JSON.stringify called with an object that doesn't "
                    "respond to #to_s.");
      return (dest = fio_bstr_write(dest, "null", 4));
    }
  // fall through
  case RUBY_T_STRING:
    dest = fio_bstr_write(dest, "\"", 1);
    dest = fio_bstr_write_escape(dest, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    dest = fio_bstr_write(dest, "\"", 1);
    return dest;
  }
}

/* *****************************************************************************
FIOBJ => Ruby Bridge
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
  rb_hash_aset((VALUE)e->udata, k, iodine_fiobj2ruby(e->value));
  return 0;
}

/** Converts FIOBJ to VALUE. Does NOT place VALUE in STORE automatically. */
static VALUE iodine_fiobj2ruby(FIOBJ o) {
  VALUE r;
  switch (FIOBJ_TYPE(o)) {
  case FIOBJ_T_TRUE: return Qtrue;
  case FIOBJ_T_FALSE: return Qfalse;
  case FIOBJ_T_NUMBER: return RB_LL2NUM(fiobj_num2i(o));
  case FIOBJ_T_FLOAT: return rb_float_new(fiobj_float2f(o));
  case FIOBJ_T_STRING: return rb_str_new(fiobj_str_ptr(o), fiobj_str_len(o));
  case FIOBJ_T_ARRAY:
    r = rb_ary_new_capa(fiobj_array_count(o));
    STORE.hold(r);
    fiobj_array_each(o, iodine_fiobj2ruby_array_task, (void *)r, 0);
    STORE.release(r);
    return r;
  case FIOBJ_T_HASH:
    rb_gc_disable();
    r = rb_hash_new();
    fiobj_hash_each(o, iodine_fiobj2ruby_hash_task, (void *)r, 0);
    rb_gc_enable();
    return r;
  // case FIOBJ_T_NULL: /* fall through */
  // case FIOBJ_T_INVALID: /* fall through */
  default: return Qnil;
  }
}

/* *****************************************************************************
JSON Parser - FIOBJ to Ruby
***************************************************************************** */

/** Accepts a JSON String and returns a Ruby object. */
static VALUE iodine_json_parse_indirect(VALUE self, VALUE rstr) {
  VALUE r = Qnil;
  rb_check_type(rstr, RUBY_T_STRING);
  size_t consumed = 0;
  FIOBJ tmp =
      fiobj_json_parse((fio_str_info_s)IODINE_RSTR_INFO(rstr), &consumed);
  r = iodine_fiobj2ruby(tmp);
  fiobj_free(tmp);
  return r;
}

/* *****************************************************************************
JSON Parser (direct 2 Ruby)
***************************************************************************** */

/** The JSON parser settings. */
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
  STORE.hold(str);
  return (void *)str;
}
FIO_SFUNC void *iodine___json_on_string_simple(const void *start, size_t len) {
  VALUE str = rb_str_new((const char *)start, len);
  STORE.hold(str);
  return (void *)str;
}
/** Dictionary was detected. Returns ctx to hash map or NULL on error. */
FIO_SFUNC void *iodine___json_on_map(void *ctx, void *at) {
  (void)ctx, (void)at;
  VALUE map = rb_hash_new();
  STORE.hold(map);
  return (void *)map;
}
/** Array was detected. Returns ctx to array or NULL on error. */
FIO_SFUNC void *iodine___json_on_array(void *ctx, void *at) {
  (void)ctx, (void)at;
  VALUE ary = rb_ary_new();
  STORE.hold(ary);
  return (void *)ary;
}
/** Array was detected. Returns non-zero on error. */
FIO_SFUNC int iodine___json_map_push(void *ctx, void *key, void *val) {
  rb_hash_aset((VALUE)ctx, (VALUE)key, (VALUE)val);
  STORE.release((VALUE)val);
  STORE.release((VALUE)key);
  return 0;
}
/** Array was detected. Returns non-zero on error. */
FIO_SFUNC int iodine___json_array_push(void *ctx, void *val) {
  rb_ary_push((VALUE)ctx, (VALUE)val);
  STORE.release((VALUE)val);
  return 0;
}
/** Called for the `key` element in case of error or NULL value. */
FIO_SFUNC void iodine___json_free_unused_object(void *ctx) {
  STORE.release((VALUE)ctx);
}
/** the JSON parsing encountered an error - what to do with ctx? */
FIO_SFUNC void *iodine___json_on_error(void *ctx) {
  STORE.release((VALUE)ctx);
  return (void *)Qnil;
}

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
API
***************************************************************************** */

/** Accepts a JSON String and returns a Ruby object. */
static VALUE iodine_json_parse(VALUE self, VALUE rstr) {
  rb_check_type(rstr, RUBY_T_STRING);
  fio_json_result_s r = fio_json_parse(&IODINE_JSON_PARSER_CALLBACKS,
                                       RSTRING_PTR(rstr),
                                       RSTRING_LEN(rstr));
  STORE.release((VALUE)r.ctx);
  return (VALUE)r.ctx;
}

/** Accepts a Ruby object and returns a JSON String. */
static VALUE iodine_json_stringify(VALUE self, VALUE object) {
  VALUE r = Qnil;
  char *str = fio_bstr_reserve(NULL, ((size_t)1 << 12) - 64);
  str = iodine_json_stringify2bstr(str, object);
  r = rb_str_new(str, (long)fio_bstr_len(str));
  fio_bstr_free(str);
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
  rb_define_singleton_method(m, "dump", iodine_json_stringify, 1);
}
#endif /* H___IODINE_JSON___H */
