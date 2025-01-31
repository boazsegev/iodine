#ifndef H___IODINE_LISTENER___H
#define H___IODINE_LISTENER___H
#include "iodine.h"

/* *****************************************************************************
Ruby Object.
***************************************************************************** */
static VALUE iodine_rb_IODINE_BASE_LISTENER;

typedef struct {
  void *listener;
  VALUE handler;
  bool is_http;
} iodine_listener_s;

static void iodine_listener_free(void *p) {
  iodine_listener_s *m = (iodine_listener_s *)p;
  ruby_xfree(m);
}

static size_t iodine_listener_size(const void *p) {
  iodine_listener_s *m = (iodine_listener_s *)p;
  return sizeof(*m);
}

FIO_IFUNC void iodine_listener_gc_mark(void *p) {
  iodine_listener_s *m = (iodine_listener_s *)p;
  if (!IODINE_STORE_IS_SKIP(m->handler))
    rb_gc_mark(m->handler);
}

static const rb_data_type_t IODINE_LISTNER_DATA_TYPE = {
    .wrap_struct_name = "IodineListner",
    .function =
        {
            .dmark = iodine_listener_gc_mark,
            .dfree = iodine_listener_free,
            .dsize = iodine_listener_size,
        },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static iodine_listener_s *iodine_listener_ptr(VALUE self) {
  iodine_listener_s *m;
  TypedData_Get_Struct(self, iodine_listener_s, &IODINE_LISTNER_DATA_TYPE, m);
  return m;
}

static VALUE iodine_listener_alloc(VALUE klass) {
  iodine_listener_s *m;
  VALUE self = TypedData_Make_Struct(klass,
                                     iodine_listener_s,
                                     &IODINE_LISTNER_DATA_TYPE,
                                     m);
  *m = (iodine_listener_s){0};
  return self;
}

/* *****************************************************************************
Helpers
***************************************************************************** */

#define IODINE_LISTNER_ONSET(o)                                                \
  do {                                                                         \
    STORE.release(old_value);                                                  \
    STORE.hold(o->handler);                                                    \
    iodine_handler_method_injection__inner(iodine_rb_IODINE_BASE_LISTENER,     \
                                           o->handler,                         \
                                           0);                                 \
  } while (0)

FIO_DEF_GETSET_FUNC(static,
                    iodine___listener,
                    iodine_listener_s,
                    VALUE,
                    handler,
                    IODINE_LISTNER_ONSET)

#undef IODINE_LISTNER_ONSET

static VALUE iodine_listener_new(void *listener, VALUE handler, bool is_http) {
  VALUE r = iodine_listener_alloc(iodine_rb_IODINE_BASE_LISTENER);
  if (IODINE_STORE_IS_SKIP(r))
    rb_raise(rb_eNoMemError, "Listener loocation error!");
  iodine_listener_s *l = iodine_listener_ptr(r);
  *l = (iodine_listener_s){.listener = listener,
                           .handler = handler,
                           .is_http = is_http};
  return r;
}
/* *****************************************************************************
API
***************************************************************************** */

static VALUE iodine_listener_handler_get(VALUE o) {
  iodine_listener_s *l = iodine_listener_ptr(o);
  if (!l->listener)
    rb_raise(rb_eRuntimeError, "Did you try to create this object manually?");
  return iodine___listener_handler(l);
}
static VALUE iodine_listener_handler_set(VALUE o, VALUE h) {
  if (IODINE_STORE_IS_SKIP(h))
    rb_raise(rb_eTypeError,
             "Listener handler must be a valid object with propper callbacks. "
             "See iodine documentation.");
  iodine_listener_s *l = iodine_listener_ptr(o);
  if (!l->listener)
    rb_raise(rb_eRuntimeError, "Did you try to create this object manually?");
  if (l->is_http)
    fio_http_listener_settings(l->listener)->udata = (void *)h;
  else
    fio_io_listener_udata_set(l->listener, (void *)h);
  return iodine___listener_handler_set(l, h);
}

static VALUE iodine_listener_initialize(VALUE o) {
  rb_raise(rb_eRuntimeError,
           "Iodine Listeners can only be created using Iodine.listen");
  return Qnil;
}

/* *****************************************************************************
Initialize

Benchmark with:

require 'iodine/benchmark'
Iodine::Benchmark.minimap(100)

m = Iodine::Base::MiniMap.new
10.times {|i| m[i] = i }
m.each {|k,v| puts "#{k.to_s} => #{v.to_s}"}
***************************************************************************** */
static void Init_Iodine_Listener(void) {
  VALUE m = iodine_rb_IODINE_BASE_LISTENER =
      rb_define_class_under(iodine_rb_IODINE_BASE, "Listener", rb_cObject);
  STORE.hold(iodine_rb_IODINE_BASE_LISTENER);
  rb_define_alloc_func(m, iodine_listener_alloc);
  rb_define_method(m, "initialize", iodine_listener_initialize, 0);
  rb_define_method(m, "handler", iodine_listener_handler_get, 0);
  rb_define_method(m, "handler=", iodine_listener_handler_set, 1);
}
#endif /* H___IODINE_LISTENER___H */
