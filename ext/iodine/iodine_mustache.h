#ifndef H___IODINE_MUSTA___H
#define H___IODINE_MUSTA___H
#include "iodine.h"

/* *****************************************************************************
Mustache Callbacks
***************************************************************************** */

static void *mus_get_var(void *ctx, fio_buf_info_s name) {
  VALUE r = Qnil;
  VALUE c = (VALUE)ctx;
  ID to_hash;
  fio_buf_info_s buf;
  int64_t index;
  if (TYPE(c) == RUBY_T_ARRAY)
    goto is_an_array;
  if (TYPE(c) != RUBY_T_HASH)
    goto not_a_hash;
  r = rb_hash_aref(c, rb_id2sym(rb_intern2(name.buf, name.len)));
  if (FIO_LIKELY(r != Qnil))
    goto found;
  r = rb_hash_aref(c, rb_str_new_static(name.buf, name.len));
  if (FIO_LIKELY(r != Qnil))
    goto found;
  buf = name;
  index = fio_atol(&buf.buf);
  if (buf.buf == name.buf + name.len) {
    r = rb_hash_aref(c, LL2NUM(index));
  }
  if (r == Qnil)
    r = (VALUE)NULL;
found:
  STORE.hold(r);
  return (void *)r;

not_a_hash:
  if (iodine_is_minimap(c))
    goto is_minimap;
  to_hash = rb_intern2("to_hash", 7);
  if (c && TYPE(c) == RUBY_T_OBJECT && rb_respond_to(c, to_hash))
    return mus_get_var((void *)rb_funcallv(c, to_hash, 0, &c), name);
  return NULL;

is_minimap:
  r = iodine_minimap_get(c, rb_id2sym(rb_intern2(name.buf, name.len)));
  if (FIO_LIKELY(r != Qnil))
    goto found;
  r = iodine_minimap_get(c, rb_str_new_static(name.buf, name.len));
  if (FIO_LIKELY(r != Qnil))
    goto found;
  buf = name;
  index = fio_atol(&buf.buf);
  if (buf.buf == name.buf + name.len) {
    r = iodine_minimap_get(c, LL2NUM(index));
  }
  if (r == Qnil)
    r = (VALUE)NULL;
  goto found;

is_an_array:
  buf = name;
  if (name.len == 6 && fio_buf2u32u(name.buf) == fio_buf2u32u("leng") &&
      fio_buf2u16u(name.buf + 4) == fio_buf2u16u("th"))
    return (void *)LL2NUM(rb_array_len(c));
  index = fio_atol(&buf.buf);
  if (buf.buf == name.buf + name.len) {
    r = rb_ary_entry(c, (long)index);
    if (FIO_LIKELY(r != Qnil))
      goto found;
  }
  return NULL;
}
static size_t mus_get_array_len(void *ctx) {
  VALUE c = (VALUE)ctx;
  if (TYPE(c) != RUBY_T_ARRAY)
    return 0;
  return RARRAY_LEN(c);
}

static void *mus_get_var_index(void *ctx, size_t index) {
  VALUE c = (VALUE)ctx;
  if (TYPE(c) != RUBY_T_ARRAY)
    return NULL;
  c = rb_ary_entry(c, index);
  STORE.hold(c);
  return (void *)c;
}
static fio_buf_info_s mus_var2str(void *var) {
  if ((VALUE)var == Qnil || !var)
    return FIO_BUF_INFO2(NULL, 0);
  VALUE v = (VALUE)var;
  switch (TYPE(v)) {
  case RUBY_T_TRUE: return FIO_BUF_INFO2((char *)"true", 4);
  case RUBY_T_FALSE: return FIO_BUF_INFO2((char *)"false", 5);
  case RUBY_T_SYMBOL: v = rb_sym2str(v);
  case RUBY_T_STRING:
    return FIO_BUF_INFO2(RSTRING_PTR(v), (size_t)RSTRING_LEN(v));
  case RUBY_T_FIXNUM: /* fall through */
  case RUBY_T_BIGNUM: /* fall through */
  case RUBY_T_FLOAT:  /* fall through */
  case RUBY_T_ARRAY:  /* fall through */
  case RUBY_T_HASH:   /* fall through */
    v = rb_funcallv(v, IODINE_TO_S_ID, 0, &v);
    return FIO_BUF_INFO2(RSTRING_PTR(v), (size_t)RSTRING_LEN(v));
  default:
    if (rb_respond_to(v, rb_intern("call"))) {
      return mus_var2str((void *)rb_proc_call(v, rb_ary_new()));
    }
    if ((v = rb_check_funcall(v, IODINE_TO_S_ID, 0, NULL)) != RUBY_Qundef) {
      if (RB_TYPE_P(v, RUBY_T_STRING))
        return FIO_BUF_INFO2(RSTRING_PTR(v), (size_t)RSTRING_LEN(v));
    }
    return FIO_BUF_INFO2(NULL, 0);
  }
}
static int mus_var_is_truthful(void *ctx) {
  return ctx && ((VALUE)ctx) != Qnil && ((VALUE)ctx) != Qfalse &&
         (TYPE(((VALUE)ctx)) != RUBY_T_ARRAY || rb_array_len(((VALUE)ctx)));
}

static void mus_release_var(void *ctx) { STORE.release((VALUE)ctx); }

static int mus_is_lambda(void **udata, void *ctx, fio_buf_info_s raw) {
  VALUE c = (VALUE)ctx;
  if (!rb_respond_to(c, rb_intern("call")))
    return 0;
  VALUE tmp = rb_ary_new();
  if (raw.len)
    rb_ary_push(tmp, rb_str_new(raw.buf, raw.len));
  tmp = rb_proc_call(c, tmp);
  fio_buf_info_s txt = mus_var2str((void *)tmp);
  if (txt.len)
    *udata = (void *)fio_bstr_write((char *)(*udata), txt.buf, txt.len);
  return 1;
}

static void mus_on_yaml_front_matter(fio_buf_info_s yaml_front_matter,
                                     void *udata) {
  VALUE block = (VALUE)udata;
  VALUE yaml_data = rb_ary_new();
  rb_ary_push(yaml_data,
              rb_str_new(yaml_front_matter.buf, yaml_front_matter.len));
  rb_proc_call(block, yaml_data);
}

/* *****************************************************************************
Ruby Object.
***************************************************************************** */

static size_t fio_mustache_wrapper_size(const void *ptr_) {
  return fio_bstr_len(*(const char **)ptr_) + 8;
}

static void fio_mustache_wrapper_free(void *p) {
  fio_mustache_s **pm = (fio_mustache_s **)p;
  fio_mustache_free(*pm);
  FIO_LEAK_COUNTER_ON_FREE(iodine_mustache);
  ruby_xfree(pm);
}

static const rb_data_type_t IODINE_MUSTACHE_DATA_TYPE = {
    .wrap_struct_name = "IodineMustache",
    .function =
        {
            .dfree = fio_mustache_wrapper_free,
            .dsize = fio_mustache_wrapper_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE fio_mustache_wrapper_alloc(VALUE klass) {
  fio_mustache_s **mp;
  VALUE o = TypedData_Make_Struct(klass,
                                  fio_mustache_s *,
                                  &IODINE_MUSTACHE_DATA_TYPE,
                                  mp);
  *mp = NULL;
  FIO_LEAK_COUNTER_ON_ALLOC(iodine_mustache);
  return o;
}

static fio_mustache_s **fio_mustache_wrapper_get(VALUE self) {
  fio_mustache_s **mp;
  return TypedData_Get_Struct(self,
                              fio_mustache_s *,
                              &IODINE_MUSTACHE_DATA_TYPE,
                              mp);
}

/* *****************************************************************************
API
***************************************************************************** */
// clang-format off
/** 
 * Loads a template file and compiles it into a flattened instruction tree.
 *
 *        Iodine::Mustache.new(file = nil, data = nil, on_yaml = nil, &block = nil)
 *
 * @param file [String=nil] a file name for the mustache template.
 *
 * @param template [String=nil] the content of the mustache template.
 *
 * @param on_yaml [Proc=nil] (optional) accepts a YAML front-matter String.
 *
 * @param &block to be used as an implicit \p on_yaml (if missing).
 *
 * @return [Iodine::Mustache] returns an Iodine::Mustache object with the
 * provided template ready for rendering.
 *
 * **Note**: Either the file or template argument (or both) must be provided.
 */
static VALUE mus_load_template(int argc, VALUE *argv, VALUE self) { // clang-format on
  VALUE on_yaml_block = Qnil;
  fio_buf_info_s fname = FIO_BUF_INFO0;
  fio_buf_info_s data = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(fname, 0, "file", 0),
                  IODINE_ARG_BUF(data, 0, "template", 0),
                  IODINE_ARG_PROC(on_yaml_block, 0, "on_yaml", 0));

  if (!fname.buf && !data.buf)
    rb_raise(rb_eArgError,
             "either template `file` or `template` should be provided.");
  if ((data.buf && !data.len) || (fname.buf && !fname.len))
    rb_raise(rb_eArgError,
             "neither template `file` nor `template` can be empty.");

  fio_mustache_s *m =
      fio_mustache_load(.data = data,
                        .filename = fname,
                        .on_yaml_front_matter =
                            (on_yaml_block == Qnil ? NULL
                                                   : mus_on_yaml_front_matter),
                        .udata = (void *)on_yaml_block);
  if (!m)
    rb_raise(rb_eStandardError,
             "template couldn't be found or empty, nothing to build.");
  fio_mustache_s **mp = fio_mustache_wrapper_get(self);
  *mp = m;
  return self;
}

/**
 * Renders the template given at initialization with the provided context.
 *
 *        m.render(ctx)
 *
 * @param ctx the top level context for the template data.
 *
 * @return [String] returns a String containing the rendered template.
 */
static VALUE mus_render(VALUE self, VALUE ctx) {
  fio_mustache_s **mp = fio_mustache_wrapper_get(self);
  if (!*mp)
    rb_raise(rb_eStandardError, "mustache template is empty, couldn't render.");
  char *result =
      (char *)fio_mustache_build(*mp,
                                 .get_var = mus_get_var,
                                 .array_length = mus_get_array_len,
                                 .get_var_index = mus_get_var_index,
                                 .var2str = mus_var2str,
                                 .var_is_truthful = mus_var_is_truthful,
                                 .release_var = mus_release_var,
                                 .is_lambda = mus_is_lambda,
                                 .ctx = (void *)ctx);
  if (!result)
    return Qnil;
  VALUE str = rb_utf8_str_new(result, fio_bstr_len(result));
  fio_bstr_free(result);
  return str;
}
// clang-format off
/**
 * Loads a template file and renders it into a String.
 *
 *        Iodine::Mustache.render(file = nil, data = nil, ctx = nil, on_yaml = nil)
 *
 * @param file [String=nil] a file name for the mustache template.
 *
 * @param template [String=nil] the content of the mustache template.
 *
 * @param ctx [String=nil] the top level context for the template data.
 *
 * @param on_yaml [Proc=nil] (optional) accepts a YAML front-matter String.
 *
 * @param &block to be used as an implicit \p on_yaml (if missing).
 *
 * @return [String] returns a String containing the rendered template.
 *
 * **Note**: Either the file or template argument (or both) must be provided.
 */
static VALUE mus_build_and_render(int argc, VALUE *argv, VALUE klass) { // clang-format on
  VALUE ctx = Qnil;
  VALUE on_yaml_block = Qnil;
  fio_buf_info_s fname = FIO_BUF_INFO2(NULL, 0);
  fio_buf_info_s data = FIO_BUF_INFO2(NULL, 0);

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(fname, 0, "file", 0),
                  IODINE_ARG_BUF(data, 0, "template", 0),
                  IODINE_ARG_RB(ctx, 0, "ctx", 0),
                  IODINE_ARG_PROC(on_yaml_block, 0, "on_yaml", 0));

  if (!fname.buf && !data.buf)
    rb_raise(rb_eArgError,
             "either template `file` or `template` should be provided.");
  if ((data.buf && !data.len) || (fname.buf && !fname.len))
    rb_raise(rb_eArgError,
             "neither template `file` nor `template` can be empty.");

  fio_mustache_s *m =
      fio_mustache_load(.data = data,
                        .filename = fname,
                        .on_yaml_front_matter =
                            (on_yaml_block == Qnil ? NULL
                                                   : mus_on_yaml_front_matter),
                        .udata = (void *)on_yaml_block);
  if (!m)
    rb_raise(rb_eStandardError,
             "template couldn't be found or empty, nothing to build.");

  char *result = (char *)fio_mustache_build(m,
                                            .get_var = mus_get_var,
                                            .array_length = mus_get_array_len,
                                            .get_var_index = mus_get_var_index,
                                            .var2str = mus_var2str,
                                            .release_var = mus_release_var,
                                            .is_lambda = mus_is_lambda,
                                            .ctx = (void *)ctx);
  fio_mustache_free(m);
  if (!result)
    return Qnil;
  VALUE str = rb_utf8_str_new(result, fio_bstr_len(result));
  fio_bstr_free(result);
  return str;
}

/** Initialize Iodine::Mustache */ // clang-format off
/**
Iodine::Mustache is a lighter implementation of the mustache template rendering gem, with a focus on a few minor security details:

1. HTML escaping is more aggressive, increasing XSS protection. Read why at: [wonko.com/post/html-escaping](https://wonko.com/post/html-escaping).

2. Dot notation is tested in whole as well as in part (i.e. `user.name.first` will be tested as is, than the couplet `user`, `name.first` and than as each `user`, `name` , `first`), allowing for the Hash data to contain keys with dots while still supporting dot notation shortcuts.

3. Less logic: i.e., lambdas / procs do not automatically invoke a re-rendering... I'd remove them completely as unsafe, but for now there's that.

4. Improved Protection against Endless Recursion: i.e., Partial templates reference themselves when recursively nested (instead of being recursively re-loaded); and Partial's context is limited to their starting point's context (cannot access parent context).

It wasn't designed specifically for speed or performance... but it ended up being significantly faster.

## Usage

This approach to Mustache templates may require more forethought when designing either the template or the context's data format, however it should force implementations to be more secure and performance aware.

Approach:

          require 'iodine'
          # One-off rendering of (possibly dynamic) template:
          result = Iodine::Mustache.render(template: "{{foo}}", ctx: {foo: "bar"}) # => "bar"
          # caching of parsed template data for multiple render operations:
          view = Iodine::Mustache.new(file: "./views/foo.mustache", template: "{{foo}}")
          results = Array.new(100) {|i| view.render(foo: "bar#{i}") } # => ["bar0", "bar1", ...]

## Performance

Performance may differ according to architecture and compiler used. Please measure:

          require 'benchmark/ips'
          require 'mustache'
          require 'iodine'

          # Benchmark code was copied, in part, from:
          #   https://github.com/mustache/mustache/blob/master/benchmarks/render_collection_benchmark.rb
          # The test is, sadly, biased and doesn't test for missing elements, proc/method resolution or template partials.
          def benchmark_mustache
            template = """
            {{#products}}
              <div class='product_brick'>
                <div class='container'>
                  <div class='element'>
                    <img src='images/{{image}}' class='product_miniature' />
                  </div>
                  <div class='element description'>
                    <a href={{url}} class='product_name block bold'>
                      {{external_index}}
                    </a>
                  </div>
                </div>
              </div>
            {{/products}}
            """
            
            # fill Hash objects with values for template rendering
            data_1000 = {
              products: []
            }
            data_1000_escaped = {
              products: []
            }

            1000.times do
              data_1000[:products] << {
                :external_index=>"product",
                :url=>"/products/7",
                :image=>"products/product.jpg"
              }
              data_1000_escaped[:products] << {
                :external_index=>"This <product> should've been \"properly\" escaped.",
                :url=>"/products/7",
                :image=>"products/product.jpg"
              }
            end

            # prepare Iodine::Mustache reduced Mustache template engine
            mus_view = Iodine::Mustache.new(template: template)

            # prepare official Mustache template engine
            view = Mustache.new
            view.template = template
            view.render # Call render once so the template will be compiled

            # benchmark different use cases
            Benchmark.ips do |x|
              x.report("Ruby Mustache render list of 1000") do |times|
                view.render(data_1000)
              end
              x.report("Iodine::Mustache render list of 1000") do |times|
                mus_view.render(data_1000)
              end

              x.report("Ruby Mustache render list of 1000 with escaped data") do |times|
                view.render(data_1000_escaped)
              end
              x.report("Iodine::Mustache render list of 1000 with escaped data") do |times|
                mus_view.render(data_1000_escaped)
              end

              x.report("Ruby Mustache - no caching - render list of 1000") do |times|
                Mustache.render(template, data_1000)
              end
              x.report("Iodine::Mustache - no caching - render list of 1000") do |times|
                Iodine::Mustache.render(nil, template, data_1000)
              end
              x.compare!
            end ; nil
          end

          benchmark_mustache

*/ 
static void Init_Iodine_Mustache(void) {
  VALUE m = rb_define_class_under(iodine_rb_IODINE, "Mustache", rb_cObject); // clang-format on
  rb_define_alloc_func(m, fio_mustache_wrapper_alloc);
  rb_define_method(m, "initialize", mus_load_template, -1);
  rb_define_method(m, "render", mus_render, 1);
  rb_define_singleton_method(m, "render", mus_build_and_render, -1);
}
#endif /* H___IODINE_MUSTA___H */
