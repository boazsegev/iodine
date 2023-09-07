#include "iodine.h"

/* *****************************************************************************
Lambda Handlers - TODO?
***************************************************************************** */

/* *****************************************************************************
Mustache Callbacks
***************************************************************************** */

static void *mus_get_var(void *ctx, fio_buf_info_s name) {
  VALUE r = Qnil;
  VALUE c = (VALUE)ctx;
  ID to_hash;
  if (TYPE(c) != RUBY_T_HASH)
    goto not_a_hash;
  r = rb_hash_aref(c, rb_id2sym(rb_intern2(name.buf, name.len)));
  if (FIO_LIKELY(r != Qnil))
    return (void *)r;
  r = rb_hash_aref(c, rb_str_new_static(name.buf, name.len));
  if (r == Qnil)
    r = (VALUE)NULL;
  STORE.hold(r);
  return (void *)r;
not_a_hash:
  to_hash = rb_intern2("to_hash", 7);
  if (c && TYPE(c) == RUBY_T_OBJECT && rb_respond_to(c, to_hash))
    return mus_get_var((void *)rb_funcallv(c, to_hash, 0, &c), name);
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
  case RUBY_T_TRUE:
    return FIO_BUF_INFO2((char *)"true", 4);
  case RUBY_T_FALSE:
    return FIO_BUF_INFO2((char *)"false", 5);
  case RUBY_T_SYMBOL:
    v = rb_sym2str(v);
  case RUBY_T_STRING:
    return FIO_BUF_INFO2(RSTRING_PTR(v), RSTRING_LEN(v));
  case RUBY_T_FIXNUM: /* fall through */
  case RUBY_T_BIGNUM: /* fall through */
  case RUBY_T_FLOAT:
    v = rb_funcallv(v, rb_intern2("to_s", 4), 0, &v);
    return FIO_BUF_INFO2(RSTRING_PTR(v), RSTRING_LEN(v));
  case RUBY_T_ARRAY: /* fall through */
  case RUBY_T_HASH:  /* fall through */
  default:
    if (rb_respond_to(v, rb_intern("call"))) {
      return mus_var2str((void *)rb_proc_call(v, rb_ary_new()));
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

typedef struct fio_mustache_wrapper_s {
  fio_mustache_s *m;
} fio_mustache_wrapper_s;

static void fio_mustache_wrapper_free(void *p) {
  fio_mustache_wrapper_s *wrapper = p;
  fio_mustache_free(wrapper->m);
}

static VALUE fio_mustache_wrapper_alloc(VALUE klass) {
  fio_mustache_wrapper_s *wrapper;
  VALUE o = Data_Make_Struct(klass, fio_mustache_wrapper_s, NULL,
                             fio_mustache_wrapper_free, wrapper);
  *wrapper = (fio_mustache_wrapper_s){NULL};
  return o;
}

/* *****************************************************************************
API
***************************************************************************** */

/**
 * Loads a template file and compiles it into a flattened instruction tree.
 *
 *        Musta.new(file = nil, data = nil, on_yaml = nil, &block = nil)
 *
 * @param file [String=nil] a file name for the mustache template.
 *
 * @param template [String=nil] the content of the mustache template.
 *
 * @param on_yaml [Proc=nil] (optional) accepts a YAML front-matter String.
 *
 * @param &block to be used as an implicit \p on_yaml (if missing).
 *
 * @return [Musta] returns a Musta object with the provided template ready for
 * rendering.
 *
 * **Note**: Either the file or template argument (or both) must be provided.
 */
static VALUE mus_load_template(int argc, VALUE *argv, VALUE self) {
  VALUE file_name = Qnil;
  VALUE file_content = Qnil;
  VALUE on_yaml_block = Qnil;
  fio_buf_info_s data = FIO_BUF_INFO2(NULL, 0);
  fio_buf_info_s fname = FIO_BUF_INFO2(NULL, 0);
  fio_rb_multi_arg(argc, argv, FIO_RB_ARG(file_name, 0, "file", Qnil, 0),
                   FIO_RB_ARG(file_content, 0, "template", Qnil, 0),
                   FIO_RB_ARG(on_yaml_block, 0, "on_yaml", Qnil, 0));
  if (file_name == Qnil && file_content == Qnil)
    rb_raise(rb_eArgError,
             "either template `name` or `template` should be provided.");
  if (file_name != Qnil) {
    rb_check_type(file_name, RUBY_T_STRING);
    fname = FIO_BUF_INFO2(RSTRING_PTR(file_name), RSTRING_LEN(file_name));
  }
  if (file_content != Qnil) {
    rb_check_type(file_content, RUBY_T_STRING);
    data = FIO_BUF_INFO2(RSTRING_PTR(file_content), RSTRING_LEN(file_content));
  }
  if ((data.buf && !data.len) || (fname.buf && !fname.len))
    rb_raise(rb_eArgError,
             "neither template `name` nor `template` can be empty.");

  if (on_yaml_block == Qnil && rb_block_given_p())
    on_yaml_block = rb_block_proc();

  fio_mustache_s *m =
      fio_mustache_load(.data = data, .filename = fname,
                        .on_yaml_front_matter =
                            (on_yaml_block == Qnil ? NULL
                                                   : mus_on_yaml_front_matter),
                        .udata = (void *)on_yaml_block);
  if (!m)
    rb_raise(rb_eStandardError,
             "template couldn't be found or empty, nothing to build.");
  fio_mustache_wrapper_s *wrapper;
  Data_Get_Struct(self, fio_mustache_wrapper_s, wrapper);
  wrapper->m = m;
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
  fio_mustache_wrapper_s *wrapper;
  Data_Get_Struct(self, fio_mustache_wrapper_s, wrapper);
  if (!wrapper->m)
    rb_raise(rb_eStandardError, "mustache template is empty, couldn't render.");
  char *result = fio_mustache_build(
      wrapper->m, .get_var = mus_get_var, .array_length = mus_get_array_len,
      .get_var_index = mus_get_var_index, .var2str = mus_var2str,
      .var_is_truthful = mus_var_is_truthful, .release_var = mus_release_var,
      .is_lambda = mus_is_lambda, .ctx = (void *)ctx);
  if (!result)
    return Qnil;
  VALUE str = rb_utf8_str_new(result, fio_bstr_len(result));
  fio_bstr_free(result);
  return str;
}

/**
 * Loads a template file and renders it into a String.
 *
 *        Musta.render(file = nil, data = nil, ctx = nil, on_yaml = nil)
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
static VALUE mus_build_and_render(int argc, VALUE *argv, VALUE klass) {
  VALUE file_name = Qnil;
  VALUE file_content = Qnil;
  VALUE ctx = Qnil;
  VALUE on_yaml_block = Qnil;
  fio_buf_info_s data = FIO_BUF_INFO2(NULL, 0);
  fio_buf_info_s fname = FIO_BUF_INFO2(NULL, 0);
  fio_rb_multi_arg(argc, argv, FIO_RB_ARG(file_name, 0, "file", Qnil, 0),
                   FIO_RB_ARG(file_content, 0, "template", Qnil, 0),
                   FIO_RB_ARG(ctx, 0, "ctx", Qnil, 0),
                   FIO_RB_ARG(on_yaml_block, 0, "on_yaml", Qnil, 0));
  if (file_name == Qnil && file_content == Qnil)
    rb_raise(rb_eArgError,
             "either template `name` or `template` should be provided.");
  if (file_name != Qnil) {
    rb_check_type(file_name, RUBY_T_STRING);
    fname = FIO_BUF_INFO2(RSTRING_PTR(file_name), RSTRING_LEN(file_name));
  }
  if (file_content != Qnil) {
    rb_check_type(file_content, RUBY_T_STRING);
    data = FIO_BUF_INFO2(RSTRING_PTR(file_content), RSTRING_LEN(file_content));
  }
  if ((data.buf && !data.len) || (fname.buf && !fname.len))
    rb_raise(rb_eArgError,
             "neither template `name` nor `template` can be empty.");
  if (on_yaml_block == Qnil && rb_block_given_p())
    on_yaml_block = rb_block_proc();

  fio_mustache_s *m =
      fio_mustache_load(.data = data, .filename = fname,
                        .on_yaml_front_matter =
                            (on_yaml_block == Qnil ? NULL
                                                   : mus_on_yaml_front_matter),
                        .udata = (void *)on_yaml_block);
  if (!m)
    rb_raise(rb_eStandardError,
             "template couldn't be found or empty, nothing to build.");

  char *result = fio_mustache_build(
      m, .get_var = mus_get_var, .array_length = mus_get_array_len,
      .get_var_index = mus_get_var_index, .var2str = mus_var2str,
      .release_var = mus_release_var, .is_lambda = mus_is_lambda,
      .ctx = (void *)ctx);
  fio_mustache_free(m);
  if (!result)
    return Qnil;
  VALUE str = rb_utf8_str_new(result, fio_bstr_len(result));
  fio_bstr_free(result);
  return str;
}

/* *****************************************************************************
Ruby API initialization
***************************************************************************** */

/**
 * # Musta - Almost Mustache
 *
 * This is a variation of the mustache template engine system. See README for
 * details.
 */
static void Init_musta_ext(void) {
  VALUE m = rb_define_class("Musta", iodine_rb_IODINE);
  rb_define_alloc_func(m, fio_mustache_wrapper_alloc);
  rb_define_method(m, "initialize", mus_load_template, -1);
  rb_define_method(m, "render", mus_render, 1);
  rb_define_singleton_method(m, "render", mus_build_and_render, -1);
}
