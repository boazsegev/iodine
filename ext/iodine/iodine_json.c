#include "iodine.h"

#include "fiobj.h"
#include "rb-fiobj2rb.h"

static VALUE max_nesting;
static VALUE allow_nan;
static VALUE symbolize_names;
static VALUE create_additions;
static VALUE object_class;
static VALUE array_class;

static inline VALUE iodine_json_convert(VALUE str, fiobj2rb_settings_s s) {
  FIOBJ json;
  if (!fiobj_json2obj(&json, RSTRING_PTR(str), RSTRING_LEN(str)) || !json) {
    rb_raise(rb_eRuntimeError, "JSON parsing failed. Not JSON?");
    return Qnil;
  }
  VALUE ret = fiobj2rb_deep(json, s.str2sym);
  fiobj_free(json);
  return ret;
}

static inline void iodine_json_update_settings(VALUE h,
                                               fiobj2rb_settings_s *s) {
  VALUE tmp;
  if (rb_hash_aref(h, max_nesting))
    fprintf(stderr,
            "WARNING: max_nesting ignored on this JSON implementation.\n");
  if (rb_hash_aref(h, allow_nan))
    fprintf(stderr, "WARNING: allow_nan ignored on this JSON implementation. "
                    "NaN always allowed.\n");
  if (rb_hash_aref(h, create_additions))
    fprintf(stderr,
            "WARNING: create_additions ignored on this JSON implementation.\n");
  if (rb_hash_aref(h, object_class))
    fprintf(stderr,
            "WARNING: object_class ignored on this JSON implementation.\n");
  if (rb_hash_aref(h, array_class))
    fprintf(stderr,
            "WARNING: array_class ignored on this JSON implementation.\n");
  if ((tmp = rb_hash_aref(h, symbolize_names)) != Qnil) {
    if (tmp == Qtrue)
      s->str2sym = 1;
    else if (tmp == Qfalse)
      s->str2sym = 0;
  }
}

static VALUE iodine_json_parse(int argc, VALUE *argv, VALUE self) {
  fiobj2rb_settings_s s = {.str2sym = 1};
  if (argc > 2)
    rb_raise(rb_eTypeError, "function requires supports up to two arguments.");
  if (argc == 2) {
    Check_Type(argv[1], T_HASH);
    iodine_json_update_settings(argv[1], &s);
  }
  if (argc >= 1)
    Check_Type(argv[0], T_STRING);
  else
    rb_raise(rb_eTypeError, "function requires at least one argument.");
  return iodine_json_convert(argv[0], s);
  (void)self;
}

static VALUE iodine_json_parse_bang(int argc, VALUE *argv, VALUE self) {
  fiobj2rb_settings_s s = {.str2sym = 1};
  if (argc > 2)
    rb_raise(rb_eTypeError, "function requires supports up to two arguments.");
  if (argc == 2) {
    Check_Type(argv[1], T_HASH);
    iodine_json_update_settings(argv[1], &s);
  }
  if (argc >= 1)
    Check_Type(argv[0], T_STRING);
  else
    rb_raise(rb_eTypeError, "function requires at least one argument.");
  return iodine_json_convert(argv[0], s);
  (void)self;
}

void Iodine_init_json(void) {
  VALUE tmp = rb_define_module_under(Iodine, "JSON");
  max_nesting = ID2SYM(rb_intern("max_nesting"));
  allow_nan = ID2SYM(rb_intern("allow_nan"));
  symbolize_names = ID2SYM(rb_intern("symbolize_names"));
  create_additions = ID2SYM(rb_intern("create_additions"));
  object_class = ID2SYM(rb_intern("object_class"));
  array_class = ID2SYM(rb_intern("array_class"));
  rb_define_module_function(tmp, "parse", iodine_json_parse, -1);
  rb_define_module_function(tmp, "parse!", iodine_json_parse_bang, -1);
}
