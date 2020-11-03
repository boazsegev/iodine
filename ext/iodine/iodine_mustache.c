#include <ruby.h>
#define INCLUDE_MUSTACHE_IMPLEMENTATION 1
#include "mustache_parser.h"

#include "iodine.h"

#define FIO_INCLUDE_STR
#include <fio.h>

static ID call_func_id;
static VALUE filename_id;
static VALUE data_id;
static VALUE template_id;
/* *****************************************************************************
C <=> Ruby Data allocation
***************************************************************************** */

static size_t iodine_mustache_data_size(const void *c_) {
  return sizeof(mustache_s *) +
         (((mustache_s **)c_)[0]
              ? (((mustache_s **)c_)[0]->u.read_only.data_length +
                 (((mustache_s **)c_)[0]->u.read_only.intruction_count *
                  sizeof(struct mustache__instruction_s)))
              : 0);
  (void)c_;
}

static void iodine_mustache_data_free(void *c_) {
  mustache_free(((mustache_s **)c_)[0]);
  FIO_LOG_DEBUG("deallocated mustache data at: %p", ((void **)c_)[0]);
  free((void *)c_);
  FIO_LOG_DEBUG("deallocated mustache pointer at: %p", c_);
  (void)c_;
}

static const rb_data_type_t iodine_mustache_data_type = {
    .wrap_struct_name = "IodineMustacheData",
    .function =
        {
            .dmark = NULL,
            .dfree = iodine_mustache_data_free,
            .dsize = iodine_mustache_data_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/* Iodine::PubSub::Engine.allocate */
static VALUE iodine_mustache_data_alloc_c(VALUE self) {
  void *m = malloc(sizeof(mustache_s *));
  ((mustache_s **)m)[0] = NULL;
  FIO_LOG_DEBUG("allocated mustache pointer at: %p", m);
  return TypedData_Wrap_Struct(self, &iodine_mustache_data_type, m);
}

/* *****************************************************************************
Parser Callbacks
***************************************************************************** */

static inline VALUE fiobj_mustache_find_obj_absolute(VALUE udata,
                                                     const char *name,
                                                     uint32_t name_len) {
  VALUE tmp;
  if (!RB_TYPE_P(udata, T_HASH)) {
    if (name_len == 1 && name[0] == '.')
      return udata;
    /* search by method */
    ID name_id = rb_intern2(name, name_len);
    if (rb_respond_to(udata, name_id)) {
      return IodineCaller.call(udata, name_id);
    }
    return Qnil;
  }
  /* search by Symbol */
  ID name_id = rb_intern2(name, name_len);
  VALUE key = ID2SYM(name_id);
  tmp = rb_hash_lookup2(udata, key, Qundef);
  if (tmp != Qundef)
    return tmp;
  /* search by String */
  key = rb_sym2str(key);
  tmp = rb_hash_lookup2(udata, key, Qundef);
  rb_str_free(key);
  if (tmp != Qundef)
    return tmp;
  /* search by method */
  tmp = Qnil;
  if (rb_respond_to(udata, name_id)) {
    tmp = IodineCaller.call(udata, name_id);
  }

  return tmp;
}

static inline VALUE fiobj_mustache_find_obj_tree(mustache_section_s *section,
                                                 const char *name,
                                                 uint32_t name_len) {
  do {
    VALUE tmp = fiobj_mustache_find_obj_absolute((VALUE)section->udata2, name,
                                                 name_len);
    if (tmp != Qnil) {
      return tmp;
    }
  } while ((section = mustache_section_parent(section)));
  return Qnil;
}

static inline VALUE fiobj_mustache_find_obj(mustache_section_s *section,
                                            const char *name,
                                            uint32_t name_len) {
  VALUE tmp = fiobj_mustache_find_obj_tree(section, name, name_len);
  if (tmp != Qnil)
    return tmp;
  /* interpolate sections... */
  uint32_t dot = 0;
  while (dot < name_len && name[dot] != '.')
    ++dot;
  if (dot == name_len)
    return Qnil;
  tmp = fiobj_mustache_find_obj_tree(section, name, dot);
  if (!tmp) {
    return Qnil;
  }
  ++dot;
  for (;;) {
    VALUE obj =
        fiobj_mustache_find_obj_absolute(tmp, name + dot, name_len - dot);
    if (obj != Qnil)
      return obj;
    name += dot;
    name_len -= dot;
    dot = 0;
    while (dot < name_len && name[dot] != '.')
      ++dot;
    if (dot == name_len) {
      return Qnil;
    }
    tmp = fiobj_mustache_find_obj_absolute(tmp, name, dot);
    if (tmp == Qnil)
      return Qnil;
    ++dot;
  }
}
/**
 * Called when an argument name was detected in the current section.
 *
 * A conforming implementation will search for the named argument both in the
 * existing section and all of it's parents (walking backwards towards the root)
 * until a value is detected.
 *
 * A missing value should be treated the same as an empty string.
 *
 * A conforming implementation will output the named argument's value (either
 * HTML escaped or not, depending on the `escape` flag) as a string.
 */
static int mustache_on_arg(mustache_section_s *section, const char *name,
                           uint32_t name_len, unsigned char escape) {
  VALUE o = fiobj_mustache_find_obj(section, name, name_len);
  switch (o) {
  case Qnil:
  case Qfalse:
    return 0;
  case Qtrue:
    fio_str_write(section->udata1, "true", 4);
    break;
  }
  if (!RB_TYPE_P(o, T_STRING)) {
    if (rb_respond_to(o, call_func_id))
      o = IodineCaller.call(o, call_func_id);
    if (!RB_TYPE_P(o, T_STRING))
      o = IodineCaller.call(o, iodine_to_s_id);
  }
  if (!RB_TYPE_P(o, T_STRING) || !RSTRING_LEN(o))
    return 0;
  return mustache_write_text(section, RSTRING_PTR(o), RSTRING_LEN(o), escape);
}

/**
 * Called when simple template text (string) is detected.
 *
 * A conforming implementation will output data as a string (no escaping).
 */
static int mustache_on_text(mustache_section_s *section, const char *data,
                            uint32_t data_len) {
  fio_str_write(section->udata1, data, data_len);
  return 0;
}

/**
 * Called for nested sections, must return the number of objects in the new
 * subsection (depending on the argument's name).
 *
 * Arrays should return the number of objects in the array.
 *
 * `true` values should return 1.
 *
 * `false` values should return 0.
 *
 * A return value of -1 will stop processing with an error.
 *
 * Please note, this will handle both normal and inverted sections.
 */
static int32_t mustache_on_section_test(mustache_section_s *section,
                                        const char *name, uint32_t name_len,
                                        uint8_t callable) {
  VALUE o = fiobj_mustache_find_obj(section, name, name_len);
  if (o == Qnil || o == Qfalse) {
    return 0;
  }
  if (RB_TYPE_P(o, T_ARRAY)) {
    return RARRAY_LEN(o);
  }
  if (callable && rb_respond_to(o, call_func_id)) {
    size_t len;
    const char *txt = mustache_section_text(section, &len);
    VALUE str = Qnil;
    if (txt && len) {
      str = rb_str_new(txt, len);
    }
    o = IodineCaller.call2(o, call_func_id, 1, &str);
    if (!RB_TYPE_P(o, T_STRING))
      o = rb_funcall2(o, iodine_to_s_id, 0, NULL);
    if (RB_TYPE_P(o, T_STRING) && RSTRING_LEN(o))
      mustache_write_text(section, RSTRING_PTR(o), RSTRING_LEN(o), 0);
    return 0;
  }
  return 1;
}

/**
 * Called when entering a nested section.
 *
 * `index` is a zero based index indicating the number of repetitions that
 * occurred so far (same as the array index for arrays).
 *
 * A return value of -1 will stop processing with an error.
 *
 * Note: this is a good time to update the subsection's `udata` with the value
 * of the array index. The `udata` will always contain the value or the parent's
 * `udata`.
 */
static int mustache_on_section_start(mustache_section_s *section,
                                     char const *name, uint32_t name_len,
                                     uint32_t index) {
  VALUE o = fiobj_mustache_find_obj(section, name, name_len);
  if (RB_TYPE_P(o, T_ARRAY))
    section->udata2 = (void *)rb_ary_entry(o, index);
  else if (RB_TYPE_P(o, T_HASH))
    section->udata2 = (void *)o;
  return 0;
}

/**
 * Called for cleanup in case of error.
 */
static void mustache_on_formatting_error(void *udata1, void *udata2) {
  (void)udata1;
  (void)udata2;
}

/* *****************************************************************************
Loading the template
***************************************************************************** */

/**
Loads the mustache template found in `:filename`. If `:template` is provided it
will be used instead of reading the file's content.

    Iodine::Mustache.new(filename, template = nil)

When template data is provided, filename (if any) will only be used for partial
template path resolution and the template data will be used for the template's
content. This allows, for example, for front matter to be extracted before
parsing the template.

Once a template was loaded, it could be rendered using {render}.

Accepts named arguments as well:

    Iodine::Mustache.new(filename: "foo.mustache", template: "{{ bar }}")

*/
static VALUE iodine_mustache_new(int argc, VALUE *argv, VALUE self) {
  VALUE filename = Qnil, template = Qnil;
  if (argc == 1 && RB_TYPE_P(argv[0], T_HASH)) {
    /* named arguments */
    filename = rb_hash_aref(argv[0], filename_id);
    template = rb_hash_aref(argv[0], template_id);
  } else {
    /* regular arguments */
    if (argc == 0 || argc > 2)
      rb_raise(rb_eArgError, "expecting 1..2 arguments or named arguments.");
    filename = argv[0];
    if (argc > 1) {
      template = argv[1];
    }
  }
  if (filename == Qnil && template == Qnil)
    rb_raise(rb_eArgError, "need either template contents or file name.");

  if (template != Qnil)
    Check_Type(template, T_STRING);
  if (filename != Qnil)
    Check_Type(filename, T_STRING);

  mustache_s **m = NULL;
  TypedData_Get_Struct(self, mustache_s *, &iodine_mustache_data_type, m);
  if (!m) {
    rb_raise(rb_eRuntimeError, "Iodine::Mustache allocation error.");
  }

  mustache_error_en err;
  *m = mustache_load(.filename =
                         (filename == Qnil ? NULL : RSTRING_PTR(filename)),
                     .filename_len =
                         (filename == Qnil ? 0 : RSTRING_LEN(filename)),
                     .data = (template == Qnil ? NULL : RSTRING_PTR(template)),
                     .data_len = (template == Qnil ? 0 : RSTRING_LEN(template)),
                     .err = &err);
  if (!*m)
    goto error;

  FIO_LOG_DEBUG("allocated / loaded mustache data at: %p", (void *)*m);

  return self;
error:
  switch (err) {
  case MUSTACHE_OK:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template ok, unknown error.");
    break;
  case MUSTACHE_ERR_TOO_DEEP:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache element nesting too deep.");
    break;
  case MUSTACHE_ERR_CLOSURE_MISMATCH:
    rb_raise(rb_eRuntimeError,
             "Iodine::Mustache template error, closure mismatch.");
    break;
  case MUSTACHE_ERR_FILE_NOT_FOUND:
    rb_raise(rb_eLoadError, "Iodine::Mustache template not found.");
    break;
  case MUSTACHE_ERR_FILE_TOO_BIG:
    rb_raise(rb_eLoadError, "Iodine::Mustache template too big.");
    break;
  case MUSTACHE_ERR_FILE_NAME_TOO_LONG:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template name too long.");
    break;
  case MUSTACHE_ERR_EMPTY_TEMPLATE:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template is empty.");
    break;
  case MUSTACHE_ERR_UNKNOWN:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache unknown error.");
    break;
  case MUSTACHE_ERR_USER_ERROR:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache internal error.");
    break;
  case MUSTACHE_ERR_FILE_NAME_TOO_SHORT:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template file name too long.");

    break;
  case MUSTACHE_ERR_DELIMITER_TOO_LONG:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache new delimiter is too long.");

    break;
  case MUSTACHE_ERR_NAME_TOO_LONG:
    rb_raise(rb_eRuntimeError,
             "Iodine::Mustache section name in template is too long.");
  default:
    break;
  }
  return self;
}

/* *****************************************************************************
Rendering
***************************************************************************** */

/**
Renders the mustache template using the data provided in the `data` argument.

Returns a String with the rendered template.

Raises an exception on error.

NOTE:

As one might notice, no binding is provided. Instead, a `data` Hash is assumed.
Iodine will search the Hash for any data while protecting against code
execution.
*/
static VALUE iodine_mustache_render(VALUE self, VALUE data) {
  fio_str_s str = FIO_STR_INIT;
  mustache_s **m = NULL;
  TypedData_Get_Struct(self, mustache_s *, &iodine_mustache_data_type, m);
  if (!m) {
    rb_raise(rb_eRuntimeError, "Iodine::Mustache allocation error.");
  }
  if (mustache_build(*m, .udata1 = &str, .udata2 = (void *)data))
    goto error;
  fio_str_info_s i = fio_str_info(&str);
  VALUE ret = rb_str_new(i.data, i.len);
  fio_str_free(&str);
  return ret;

error:
  fio_str_free(&str);
  rb_raise(rb_eRuntimeError, "Couldn't build template frome data.");
}

/**
Renders the mustache template found in `filename`, using the data provided in
the `data` argument. If `template` is provided it will be used instead of
reading the file's content.

    Iodine::Mustache.render(filename, data, template = nil)

Returns a String with the rendered template.

Raises an exception on error.

    template = "<h1>{{title}}</h1>"
    filename = "templates/index"
    data = {title: "Home"}
    result = Iodine::Mustache.render(filename, data)

    # filename will be used to resolve the path to any partials:
    result = Iodine::Mustache.render(filename, data, template)

    # OR, if we don't need partial template path resolution
    result = Iodine::Mustache.render(template: template, data: data)

NOTE 1:

This function doesn't cache the template data.

The more complext the template the higher the cost of the template parsing
stage.

Consider creating a persistent template object using a new object and using the
instance {#render} method.

NOTE 2:

As one might notice, no binding is provided. Instead, a `data` Hash is assumed.
Iodine will search the Hash for any data while protecting against code
execution.
*/
static VALUE iodine_mustache_render_klass(int argc, VALUE *argv, VALUE self) {
  VALUE filename = Qnil, data = Qnil, template = Qnil;
  if (argc == 1) {
    /* named arguments */
    Check_Type(argv[0], T_HASH);
    filename = rb_hash_aref(argv[0], filename_id);
    data = rb_hash_aref(argv[0], data_id);
    template = rb_hash_aref(argv[0], template_id);
  } else {
    /* regular arguments */
    if (argc < 2 || argc > 3)
      rb_raise(rb_eArgError, "expecting 2..3 arguments or named arguments.");
    filename = argv[0];
    data = argv[1];
    if (argc > 2) {
      template = argv[2];
    }
  }
  if (filename == Qnil && template == Qnil)
    rb_raise(rb_eArgError, "need either template contents or file name.");

  if (template != Qnil)
    Check_Type(template, T_STRING);
  if (filename != Qnil)
    Check_Type(filename, T_STRING);

  fio_str_s str = FIO_STR_INIT;

  mustache_s *m = NULL;
  mustache_error_en err;
  m = mustache_load(.filename =
                        (filename == Qnil ? NULL : RSTRING_PTR(filename)),
                    .filename_len =
                        (filename == Qnil ? 0 : RSTRING_LEN(filename)),
                    .data = (template == Qnil ? NULL : RSTRING_PTR(template)),
                    .data_len = (template == Qnil ? 0 : RSTRING_LEN(template)),
                    .err = &err);
  if (!m)
    goto error;
  int e = mustache_build(m, .udata1 = &str, .udata2 = (void *)data);
  mustache_free(m);
  if (e)
    goto render_error;
  fio_str_info_s i = fio_str_info(&str);
  VALUE ret = rb_str_new(i.data, i.len);
  fio_str_free(&str);
  return ret;

error:
  switch (err) {
  case MUSTACHE_OK:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template ok, unknown error.");
    break;
  case MUSTACHE_ERR_TOO_DEEP:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache element nesting too deep.");
    break;
  case MUSTACHE_ERR_CLOSURE_MISMATCH:
    rb_raise(rb_eRuntimeError,
             "Iodine::Mustache template error, closure mismatch.");
    break;
  case MUSTACHE_ERR_FILE_NOT_FOUND:
    rb_raise(rb_eLoadError, "Iodine::Mustache template not found.");
    break;
  case MUSTACHE_ERR_FILE_TOO_BIG:
    rb_raise(rb_eLoadError, "Iodine::Mustache template too big.");
    break;
  case MUSTACHE_ERR_FILE_NAME_TOO_LONG:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template name too long.");
    break;
  case MUSTACHE_ERR_EMPTY_TEMPLATE:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template is empty.");
    break;
  case MUSTACHE_ERR_UNKNOWN:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache unknown error.");
    break;
  case MUSTACHE_ERR_USER_ERROR:
    rb_raise(rb_eRuntimeError,
             "Iodine::Mustache internal error or unexpected data structure.");
    break;
  case MUSTACHE_ERR_FILE_NAME_TOO_SHORT:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template file name too long.");

    break;
  case MUSTACHE_ERR_DELIMITER_TOO_LONG:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache new delimiter is too long.");

    break;
  case MUSTACHE_ERR_NAME_TOO_LONG:
    rb_raise(rb_eRuntimeError,
             "Iodine::Mustache section name in template is too long.");

    break;
  default:
    break;
  }
  return Qnil;

render_error:
  fio_str_free(&str);
  rb_raise(rb_eRuntimeError, "Couldn't build template frome data.");
}

/* *****************************************************************************
Initialize Iodine::Mustache
***************************************************************************** */

void iodine_init_mustache(void) {
  call_func_id = rb_intern2("call", 4);
  filename_id = rb_id2sym(rb_intern2("filename", 8));
  data_id = rb_id2sym(rb_intern2("data", 4));
  template_id = rb_id2sym(rb_intern2("template", 8));
  rb_global_variable(&filename_id);
  rb_global_variable(&data_id);
  rb_global_variable(&template_id);
  VALUE tmp = rb_define_class_under(IodineModule, "Mustache", rb_cData);
  rb_define_alloc_func(tmp, iodine_mustache_data_alloc_c);
  rb_define_method(tmp, "initialize", iodine_mustache_new, -1);
  rb_define_method(tmp, "render", iodine_mustache_render, 1);
  rb_define_singleton_method(tmp, "render", iodine_mustache_render_klass, -1);
  // rb_define_module_function(tmp, "render", iodine_mustache_render_klass, 2);
}
