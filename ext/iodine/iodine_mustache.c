#include <ruby.h>
#define INCLUDE_MUSTACHE_IMPLEMENTATION 1
#include "mustache_parser.h"

#include "iodine.h"

#define FIO_INCLUDE_STR
#include <fio.h>

static ID call_func_id;
static ID to_s_func_id;
static ID filename_id;
static ID data_id;
static ID template_id;
/* *****************************************************************************
C <=> Ruby Data allocation
***************************************************************************** */

static size_t iodine_mustache_data_size(const void *c_) {
  return sizeof(mustache_s *);
  (void)c_;
}

static void iodine_mustache_data_free(void *c_) {
  mustache_free(((mustache_s **)c_)[0]);
  free((void *)c_);
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
  return TypedData_Wrap_Struct(self, &iodine_mustache_data_type, m);
}

/* *****************************************************************************
Parser Callbacks
***************************************************************************** */

/** HTML ecape table, created using the following Ruby Script:

a = (0..255).to_a.map {|i| i.chr }
# 100.times {|i| a[i] = "&\#x#{ i < 16 ? "0#{i.to_s(16)}" : i.to_s(16)};"}
100.times {|i| a[i] = "&\##{i.to_s(10)};"}
('a'.ord..'z'.ord).each {|i| a[i] = i.chr }
('A'.ord..'Z'.ord).each {|i| a[i] = i.chr }
('0'.ord..'9'.ord).each {|i| a[i] = i.chr }
a['<'.ord] = "&lt;"
a['>'.ord] = "&gt;"
a['&'.ord] = "&amp;"
a['"'.ord] = "&quot;"
a["\'".ord] = "&apos;"
a['|'.ord] = "&\##{'|'.ord.to_s(10)};"

b = a.map {|s| s.length }
puts "static char *html_escape_strs[] = {", a.to_s.slice(1..-2) ,"};",
     "static uint8_t html_escape_len[] = {", b.to_s.slice(1..-2),"};"
*/
static char *html_escape_strs[] = {
    "&#0;",  "&#1;",  "&#2;",   "&#3;",  "&#4;",   "&#5;",  "&#6;",  "&#7;",
    "&#8;",  "&#9;",  "&#10;",  "&#11;", "&#12;",  "&#13;", "&#14;", "&#15;",
    "&#16;", "&#17;", "&#18;",  "&#19;", "&#20;",  "&#21;", "&#22;", "&#23;",
    "&#24;", "&#25;", "&#26;",  "&#27;", "&#28;",  "&#29;", "&#30;", "&#31;",
    "&#32;", "&#33;", "&quot;", "&#35;", "&#36;",  "&#37;", "&amp;", "&apos;",
    "&#40;", "&#41;", "&#42;",  "&#43;", "&#44;",  "&#45;", "&#46;", "&#47;",
    "0",     "1",     "2",      "3",     "4",      "5",     "6",     "7",
    "8",     "9",     "&#58;",  "&#59;", "&lt;",   "&#61;", "&gt;",  "&#63;",
    "&#64;", "A",     "B",      "C",     "D",      "E",     "F",     "G",
    "H",     "I",     "J",      "K",     "L",      "M",     "N",     "O",
    "P",     "Q",     "R",      "S",     "T",      "U",     "V",     "W",
    "X",     "Y",     "Z",      "&#91;", "&#92;",  "&#93;", "&#94;", "&#95;",
    "&#96;", "a",     "b",      "c",     "d",      "e",     "f",     "g",
    "h",     "i",     "j",      "k",     "l",      "m",     "n",     "o",
    "p",     "q",     "r",      "s",     "t",      "u",     "v",     "w",
    "x",     "y",     "z",      "{",     "&#124;", "}",     "~",     "\x7F",
    "\x80",  "\x81",  "\x82",   "\x83",  "\x84",   "\x85",  "\x86",  "\x87",
    "\x88",  "\x89",  "\x8A",   "\x8B",  "\x8C",   "\x8D",  "\x8E",  "\x8F",
    "\x90",  "\x91",  "\x92",   "\x93",  "\x94",   "\x95",  "\x96",  "\x97",
    "\x98",  "\x99",  "\x9A",   "\x9B",  "\x9C",   "\x9D",  "\x9E",  "\x9F",
    "\xA0",  "\xA1",  "\xA2",   "\xA3",  "\xA4",   "\xA5",  "\xA6",  "\xA7",
    "\xA8",  "\xA9",  "\xAA",   "\xAB",  "\xAC",   "\xAD",  "\xAE",  "\xAF",
    "\xB0",  "\xB1",  "\xB2",   "\xB3",  "\xB4",   "\xB5",  "\xB6",  "\xB7",
    "\xB8",  "\xB9",  "\xBA",   "\xBB",  "\xBC",   "\xBD",  "\xBE",  "\xBF",
    "\xC0",  "\xC1",  "\xC2",   "\xC3",  "\xC4",   "\xC5",  "\xC6",  "\xC7",
    "\xC8",  "\xC9",  "\xCA",   "\xCB",  "\xCC",   "\xCD",  "\xCE",  "\xCF",
    "\xD0",  "\xD1",  "\xD2",   "\xD3",  "\xD4",   "\xD5",  "\xD6",  "\xD7",
    "\xD8",  "\xD9",  "\xDA",   "\xDB",  "\xDC",   "\xDD",  "\xDE",  "\xDF",
    "\xE0",  "\xE1",  "\xE2",   "\xE3",  "\xE4",   "\xE5",  "\xE6",  "\xE7",
    "\xE8",  "\xE9",  "\xEA",   "\xEB",  "\xEC",   "\xED",  "\xEE",  "\xEF",
    "\xF0",  "\xF1",  "\xF2",   "\xF3",  "\xF4",   "\xF5",  "\xF6",  "\xF7",
    "\xF8",  "\xF9",  "\xFA",   "\xFB",  "\xFC",   "\xFD",  "\xFE",  "\xFF"};
static uint8_t html_escape_len[] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 5, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 5, 5, 4, 5, 4, 5, 5, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 5, 5, 5, 5, 5,
    5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 6, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

static inline VALUE fiobj_mustache_find_obj_absolute(VALUE udata,
                                                     const char *name,
                                                     uint32_t name_len) {
  VALUE tmp;
  if (!RB_TYPE_P(udata, T_HASH)) {
    if (name_len == 1 && name[0] == '.')
      return udata;
    return Qnil;
  }
  /* search by String */
  VALUE key = rb_str_new(name, name_len);
  tmp = rb_hash_aref(udata, key);
  if (tmp != Qnil)
    return tmp;
  /* search by Symbol */
  key = rb_id2sym(rb_intern2(name, name_len));
  tmp = rb_hash_aref(udata, key);
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
    if (dot == name_len)
      return Qnil;
    tmp = fiobj_mustache_find_obj_absolute(tmp, name, dot);
    if (tmp == Qnil || !RB_TYPE_P(tmp, T_HASH))
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
  if (rb_respond_to(o, call_func_id))
    goto callable;
  if (!RB_TYPE_P(o, T_STRING))
    o = IodineCaller.call(o, to_s_func_id);
  if (!RB_TYPE_P(o, T_STRING) || !RSTRING_LEN(o))
    return 0;
  if (!escape) {
    fio_str_write(section->udata1, RSTRING_PTR(o), RSTRING_LEN(o));
    return 0;
  }
  /* HTML escape */
  fio_str_info_s str = {.data = RSTRING_PTR(o), .len = RSTRING_LEN(o)};
  fio_str_info_s i = fio_str_capa_assert(
      section->udata1, fio_str_len(section->udata1) + str.len + 64);
  do {
    if (i.len + 6 >= i.capa)
      i = fio_str_capa_assert(section->udata1, i.capa + 64);
    i = fio_str_write(section->udata1, html_escape_strs[(uint8_t)str.data[0]],
                      html_escape_len[(uint8_t)str.data[0]]);
    --str.len;
    ++str.data;
  } while (str.len);
  (void)section;
  (void)name;
  (void)name_len;
  (void)escape;
  return 0;
callable:
  o = IodineCaller.call(o, call_func_id);
  if (RB_TYPE_P(o, T_STRING))
    o = rb_funcall2(o, to_s_func_id, 0, NULL);
  fio_str_write(section->udata1, RSTRING_PTR(o), RSTRING_LEN(o));
  return 0;
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
      o = rb_funcall2(o, to_s_func_id, 0, NULL);
    if (RB_TYPE_P(o, T_STRING) && RSTRING_LEN(o))
      fio_str_write(section->udata1, RSTRING_PTR(o), RSTRING_LEN(o));
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
Loads a mustache template (and any partials).

Once a template was loaded, it could be rendered using {render}.
*/
static VALUE iodine_mustache_new(VALUE self, VALUE filename) {
  mustache_s **m = NULL;
  TypedData_Get_Struct(self, mustache_s *, &iodine_mustache_data_type, m);
  if (!m) {
    rb_raise(rb_eRuntimeError, "Iodine::Mustache allocation error.");
  }
  Check_Type(filename, T_STRING);
  mustache_error_en err;
  *m = mustache_load(.filename = RSTRING_PTR(filename),
                     .filename_len = RSTRING_LEN(filename), .err = &err);
  if (!*m)
    goto error;
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
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template not found.");
    break;
  case MUSTACHE_ERR_FILE_TOO_BIG:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template too big.");
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
    rb_raise(rb_eArgError, "missing both template contents and file name.");

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
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template not found.");
    break;
  case MUSTACHE_ERR_FILE_TOO_BIG:
    rb_raise(rb_eRuntimeError, "Iodine::Mustache template too big.");
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
  to_s_func_id = rb_intern2("to_s", 4);
  filename_id = rb_intern2("filename", 8);
  data_id = rb_intern2("data", 4);
  template_id = rb_intern2("template", 8);
  /**
  Iodine::Mustache offers a logicless mustache template engine with strict HTML
  escaping (more than the basic `"<>'$`).

  This offers more security against XSS and protects against the chance of
  executing Ruby code within the template.

  You can test the parser using:

      TEMPLATE="my_template.mustache"

      require 'json'
      require 'iodine'
      TIMES = 100
      STR = IO.binread(JSON_FILENAME); nil

      JSON.parse(STR) == Iodine::JSON.parse(STR) # => true
      JSON.parse(STR,
          symbolize_names: true) == Iodine::JSON.parse(STR,
           symbolize_names: true) # => true
      JSON.parse!(STR) == Iodine::JSON.parse!(STR) # => true/false (unknown)

      # warm-up
      TIMES.times { JSON.parse STR }
      TIMES.times { Iodine::JSON.parse STR }

      Benchmark.bm do |b|
        sys = b.report("system") { TIMES.times { JSON.parse STR } }
        sys_sym = b.report("system sym") { TIMES.times { JSON.parse STR,
                                                 symbolize_names: true } }
        iodine = b.report("iodine") { TIMES.times { Iodine::JSON.parse STR } }
        iodine_sym = b.report("iodine sym") do
                           TIMES.times { Iodine::JSON.parse STR,
                                                  symbolize_names: true }
                      end
        puts "System    /    Iodine: #{sys/iodine}"
        puts "System-sym/Iodine-sym: #{sys_sym/iodine_sym}"
      end; nil


  */
  VALUE tmp = rb_define_class_under(IodineModule, "Mustache", rb_cData);
  rb_define_alloc_func(tmp, iodine_mustache_data_alloc_c);
  rb_define_method(tmp, "initialize", iodine_mustache_new, 1);
  rb_define_method(tmp, "render", iodine_mustache_render, 1);
  rb_define_singleton_method(tmp, "render", iodine_mustache_render_klass, -1);
  // rb_define_module_function(tmp, "render", iodine_mustache_render_klass, 2);
}
