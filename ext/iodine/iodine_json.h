#ifndef H___IODINE_JSON___H
#define H___IODINE_JSON___H
#include "iodine.h"

/* *****************************************************************************
JSON Callbacks
***************************************************************************** */

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
  switch (rb_type(o)) {
  case RUBY_T_ARRAY:
    dest = fio_bstr_write(dest, "[", 1);
    if (rb_array_len(o)) {
      dest = iodine_json_stringify2bstr(dest, rb_ary_entry(o, 0));
      for (long i = 1; i < rb_array_len(o); ++i) {
        dest = fio_bstr_write(dest, ",", 1);
        dest = iodine_json_stringify2bstr(dest, rb_ary_entry(o, i));
      }
    }
    dest = fio_bstr_write(dest, "]", 1);
    return dest;
  case RUBY_T_HASH:
    dest = fio_bstr_write(dest, "{", 1);
    if (rb_hash_size(o)) {
      rb_hash_foreach(o, iodine_json_hash_each_callback, (VALUE)(&dest));
      dest[fio_bstr_len(dest) - 1] = '}';
    } else
      dest = fio_bstr_write(dest, "}", 1);
    return dest;
  case RUBY_T_FIXNUM: return fio_bstr_write_i(dest, RB_NUM2LL(o));
  case RUBY_T_FLOAT: {
    FIO_STR_INFO_TMP_VAR(buf, 232);
    buf.len = fio_ftoa(buf.buf, RFLOAT_VALUE(o), 10);
    dest = fio_bstr_write(dest, buf.buf, buf.len);
    return dest;
  }
  default:
    if (RB_TYPE_P(o, RUBY_T_SYMBOL))
      o = rb_sym_to_s(o);
    if (!RB_TYPE_P(o, RUBY_T_STRING))
      o = rb_funcallv(o, rb_intern2("to_s", 4), 0, NULL);
  // fall through
  case RUBY_T_STRING:
    dest = fio_bstr_reserve(dest, 4 + RSTRING_LEN(o));
    dest = fio_bstr_write(dest, "\"", 1);
    dest = fio_bstr_write_escape(dest, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    dest = fio_bstr_write(dest, "\"", 1);
    return dest;
  }
}

/* *****************************************************************************
FIOBJ => Ruby translation later
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
  rb_hash_aset((VALUE)e->udata,
               iodine_fiobj2ruby(e->key),
               iodine_fiobj2ruby(e->value));
  return 0;
}

static VALUE iodine_fiobj2ruby(FIOBJ o) {
  /* TODO! convert FIOBJ to VALUE */
  VALUE r;
  switch (FIOBJ_TYPE(o)) {
  case FIOBJ_T_TRUE: return Qtrue;
  case FIOBJ_T_FALSE: return Qfalse;
  case FIOBJ_T_NUMBER: return RB_LL2NUM(fiobj_num2i(o));
  case FIOBJ_T_FLOAT: return rb_float_new(fiobj_float2f(o));
  case FIOBJ_T_STRING: return rb_str_new(fiobj_str_ptr(o), fiobj_str_len(o));
  case FIOBJ_T_ARRAY:
    r = rb_ary_new();
    STORE.hold(r);
    fiobj_array_each(o, iodine_fiobj2ruby_array_task, (void *)r, 0);
    STORE.release(r);
    return r;
  case FIOBJ_T_HASH:
    STORE.hold(r);
    r = rb_hash_new();
    fiobj_hash_each(o, iodine_fiobj2ruby_hash_task, (void *)r, 0);
    STORE.release(r);
    return r;
  // case FIOBJ_T_NULL: /* fall through */
  // case FIOBJ_T_INVALID: /* fall through */
  default: return Qnil;
  }
}

/* *****************************************************************************
API
***************************************************************************** */

static VALUE iodine_json_parse(VALUE self, VALUE rstr) {
  VALUE r = Qnil;
  rb_check_type(rstr, RUBY_T_STRING);
  size_t consumed = 0;
  FIOBJ o = fiobj_json_parse(
      FIO_STR_INFO2(RSTRING_PTR(rstr), (size_t)RSTRING_LEN(rstr)),
      &consumed);
  if (!o || o == FIOBJ_INVALID)
    return r;
  // if (consumed != (size_t)RSTRING_LEN(rstr)) {
  //   /* what should be done? */
  // }
  // r = iodine_fiobj2ruby(o);
  fiobj_free(o);
  return r;
}

static VALUE iodine_json_stringify(VALUE self, VALUE object) {
  char *str = iodine_json_stringify2bstr(NULL, object);
  VALUE r = rb_str_new(str, (long)fio_bstr_len(str));
  fio_bstr_free(str);
  return r;
}
/* *****************************************************************************
Ruby API initialization
***************************************************************************** */

// clang-format off
/**
Iodine::JSON is a exposes the {Iodine::Connection#write} fallback behavior when called with non-String objects.

The fallback behavior is similar to (though faster than) calling:

```ruby
client.write(Iodine::JSON.stringify(data))
```

## Performance

Performance should be... slow.

The reason is that Iodine uses a C representation for JSON Strings and Objects,
and the wrappers convert the C representation to Ruby objects, requiring an additional copy of the data.

This especially effects parsing, where more objects are allocated and copied,
whereas {Iodine::JSON.stringify} only (re)copies the String data.

That's why {Iodine::JSON.stringify} is significantly faster than the Ruby `object.to_json` approach, yet slower than `Oj.dump(object)`.

```ruby
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
    data_1000 << tmp
  end
  json_string = Iodine::JSON.stringify(data_1000)
  # benchmark stringification
  Benchmark.ips do |x|
    x.report("Ruby obj.to_json") do |times|
      data_1000.to_json
    end
    x.report("Iodine::JSON.stringify") do |times|
      Iodine::JSON.stringify(data_1000)
    end
    if(defined?(Oj))
      x.report("Oj.dump") do |times|
        Oj.dump(data_1000)
      end
    end
    x.compare!
  end ; nil
  # benchmark parsing
  Benchmark.ips do |x|
    x.report("Ruby JSON.parse") do |times|
      JSON.parse(json_string)
    end
    x.report("Iodine::JSON.parse") do |times|
      Iodine::JSON.parse(json_string)
    end
    if(defined?(Oj))
      x.report("Oj.load") do |times|
        Oj.load(json_string)
      end
    end
    x.compare!
  end ; nil
end

benchmark_json
```

*/
static void Init_iodine_json(void) { // clang-format on
  VALUE m = rb_define_module_under(iodine_rb_IODINE, "JSON");
  rb_define_singleton_method(m, "parse", iodine_json_parse, 1);
  rb_define_singleton_method(m, "stringify", iodine_json_stringify, 1);
}
#endif /* H___IODINE_JSON___H */
