#include "iodine.h"

#include "fio.h"

#include "fio_json_parser.h"
#include "fiobj.h"
#include "iodine_fiobj2rb.h"
#include "iodine_store.h"

static VALUE max_nesting;
static VALUE allow_nan;
static VALUE symbolize_names;
static VALUE create_additions;
static VALUE object_class;
static VALUE array_class;

#define FIO_ARY_NAME fio_json_stack
#define FIO_ARY_TYPE VALUE
#include "fio.h"

/* *****************************************************************************
JSON Callacks - these must be implemented in the C file that uses the parser
***************************************************************************** */

/* *****************************************************************************
JSON Parser handling (practically copied from the FIOBJ library)
***************************************************************************** */

typedef struct {
  json_parser_s p;
  VALUE key;
  VALUE top;
  VALUE target;
  fio_json_stack_s stack;
  uint8_t is_hash;
  uint8_t symbolize;
} iodine_json_parser_s;

static inline void iodine_json_add2parser(iodine_json_parser_s *p, VALUE o) {
  if (p->top) {
    if (p->is_hash) {
      if (p->key) {
        rb_hash_aset(p->top, p->key, o);
        IodineStore.remove(p->key);
        p->key = (VALUE)0;
      } else {
        p->key = o;
        IodineStore.add(o);
      }
    } else {
      rb_ary_push(p->top, o);
    }
  } else {
    IodineStore.add(o);
    p->top = o;
  }
}

/** a NULL object was detected */
static void fio_json_on_null(json_parser_s *p) {
  iodine_json_add2parser((iodine_json_parser_s *)p, Qnil);
}
/** a TRUE object was detected */
static void fio_json_on_true(json_parser_s *p) {
  iodine_json_add2parser((iodine_json_parser_s *)p, Qtrue);
}
/** a FALSE object was detected */
static void fio_json_on_false(json_parser_s *p) {
  iodine_json_add2parser((iodine_json_parser_s *)p, Qfalse);
}
/** a Numberl was detected (long long). */
static void fio_json_on_number(json_parser_s *p, long long i) {
  iodine_json_add2parser((iodine_json_parser_s *)p, LONG2NUM(i));
}
/** a Float was detected (double). */
static void fio_json_on_float(json_parser_s *p, double f) {
  iodine_json_add2parser((iodine_json_parser_s *)p, DBL2NUM(f));
}
/** a String was detected (int / float). update `pos` to point at ending */
static void fio_json_on_string(json_parser_s *p, void *start, size_t length) {
  /* Ruby overhead for a rb_str_buf_new is very high. Double copy is faster. */
  char *tmp = fio_malloc(length);
  size_t new_len = fio_json_unescape_str(tmp, start, length);
  VALUE buf;
  if (((iodine_json_parser_s *)p)->symbolize &&
      ((iodine_json_parser_s *)p)->is_hash &&
      !((iodine_json_parser_s *)p)->key) {
    ID id = rb_intern2(tmp, new_len);
    buf = rb_id2sym(id);
  } else {
    buf = rb_str_new(tmp, new_len);
  }
  iodine_json_add2parser((iodine_json_parser_s *)p, buf);
  fio_free(tmp);
}
/** a dictionary object was detected, should return 0 unless error occurred. */
static int fio_json_on_start_object(json_parser_s *p) {
  iodine_json_parser_s *pr = (iodine_json_parser_s *)p;
  if (pr->target) {
    /* push NULL, don't free the objects */
    fio_json_stack_push(&pr->stack, pr->top);
    pr->top = pr->target;
    pr->target = 0;
  } else {
    VALUE h = rb_hash_new();
    iodine_json_add2parser(pr, h);
    fio_json_stack_push(&pr->stack, pr->top);
    pr->top = h;
  }
  pr->is_hash = 1;
  return 0;
}
/** a dictionary object closure detected */
static void fio_json_on_end_object(json_parser_s *p) {
  iodine_json_parser_s *pr = (iodine_json_parser_s *)p;
  if (pr->key) {
    FIO_LOG_WARNING("(JSON parsing) malformed JSON, "
                    "ignoring dangling Hash key.");
    IodineStore.remove(pr->key);
    pr->key = (VALUE)0;
  }
  fio_json_stack_pop(&pr->stack, &pr->top);
  pr->is_hash = (TYPE(pr->top) == T_HASH);
}
/** an array object was detected */
static int fio_json_on_start_array(json_parser_s *p) {
  iodine_json_parser_s *pr = (iodine_json_parser_s *)p;
  if (pr->target)
    return -1;
  VALUE ary = rb_ary_new();
  iodine_json_add2parser(pr, ary);
  fio_json_stack_push(&pr->stack, pr->top);
  pr->top = ary;
  pr->is_hash = 0;
  return 0;
}
/** an array closure was detected */
static void fio_json_on_end_array(json_parser_s *p) {
  iodine_json_parser_s *pr = (iodine_json_parser_s *)p;
  fio_json_stack_pop(&pr->stack, &pr->top);
  pr->is_hash = (TYPE(pr->top) == T_HASH);
}
/** the JSON parsing is complete */
static void fio_json_on_json(json_parser_s *p) { (void)p; /* do nothing */ }
/** the JSON parsing is complete */
static void fio_json_on_error(json_parser_s *p) {
  iodine_json_parser_s *pr = (iodine_json_parser_s *)p;
#if DEBUG
  FIO_LOG_ERROR("JSON on error called.");
#endif
  IodineStore.remove((VALUE)fio_json_stack_get(&pr->stack, 0));
  IodineStore.remove(pr->key);
  fio_json_stack_free(&pr->stack);
  *pr = (iodine_json_parser_s){.top = 0};
}

/* *****************************************************************************
Iodine JSON Implementation
***************************************************************************** */

static inline VALUE iodine_json_convert(VALUE str, fiobj2rb_settings_s s) {

  iodine_json_parser_s p = {.top = 0, .symbolize = s.str2sym};
  size_t consumed = fio_json_parse(&p.p, RSTRING_PTR(str), RSTRING_LEN(str));
  if (!consumed || p.p.depth) {
    IodineStore.remove((VALUE)fio_json_stack_get(&p.stack, 0));
    p.top = FIOBJ_INVALID;
  }
  fio_json_stack_free(&p.stack);
  if (p.key) {
    IodineStore.remove((VALUE)p.key);
  }
  if (!p.top) {
    rb_raise(rb_eEncodingError, "Malformed JSON format.");
  }
  IodineStore.remove(p.top);
  return p.top;
}

// static inline VALUE iodine_json_convert2(VALUE str, fiobj2rb_settings_s s) {
//   FIOBJ json;
//   if (!fiobj_json2obj(&json, RSTRING_PTR(str), RSTRING_LEN(str)) || !json) {
//     rb_raise(rb_eRuntimeError, "JSON parsing failed. Not JSON?");
//     return Qnil;
//   }
//   VALUE ret = fiobj2rb_deep(json, s.str2sym);
//   fiobj_free(json);
//   IodineStore.remove(ret);
//   return ret;
// }

static inline void iodine_json_update_settings(VALUE h,
                                               fiobj2rb_settings_s *s) {
  VALUE tmp;
  if (rb_hash_aref(h, max_nesting) != Qnil)
    FIO_LOG_WARNING("max_nesting ignored on this JSON implementation.");
  if (rb_hash_aref(h, allow_nan) != Qnil)
    fprintf(stderr, "WARNING: allow_nan ignored on this JSON implementation. "
                    "NaN always allowed.\n");
  if (rb_hash_aref(h, create_additions) != Qnil)
    FIO_LOG_WARNING("create_additions ignored on this JSON implementation.");
  if (rb_hash_aref(h, object_class) != Qnil)
    FIO_LOG_WARNING("object_class ignored on this JSON implementation.");
  if (rb_hash_aref(h, array_class) != Qnil)
    FIO_LOG_WARNING("array_class ignored on this JSON implementation.");
  if ((tmp = rb_hash_aref(h, symbolize_names)) != Qnil) {
    if (tmp == Qtrue)
      s->str2sym = 1;
    else if (tmp == Qfalse)
      s->str2sym = 0;
  }
}

/**
Parse a JSON string using the iodine lenient parser (it's also faster).
*/
static VALUE iodine_json_parse(int argc, VALUE *argv, VALUE self) {
  fiobj2rb_settings_s s = {.str2sym = 0};
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

/**
Parse a JSON string using the iodine lenient parser with a default Symbol
rather than String key (this is often faster than the regular
{Iodine::JSON.parse} function).
*/
static VALUE iodine_json_parse_bang(int argc, VALUE *argv, VALUE self) {
  fiobj2rb_settings_s s = {.str2sym = 0};
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

void iodine_init_json(void) {
  /**
  Iodine::JSON offers a fast(er) JSON parser that is also lenient and supports
  some JSON extensions such as Hex number recognition and comments.

  You can test the parser using:

      JSON_FILENAME="foo.json"

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
  VALUE tmp = rb_define_module_under(IodineModule, "JSON");
  max_nesting = ID2SYM(rb_intern("max_nesting"));
  allow_nan = ID2SYM(rb_intern("allow_nan"));
  symbolize_names = ID2SYM(rb_intern("symbolize_names"));
  create_additions = ID2SYM(rb_intern("create_additions"));
  object_class = ID2SYM(rb_intern("object_class"));
  array_class = ID2SYM(rb_intern("array_class"));
  rb_define_module_function(tmp, "parse", iodine_json_parse, -1);
  rb_define_module_function(tmp, "parse!", iodine_json_parse_bang, -1);
}
