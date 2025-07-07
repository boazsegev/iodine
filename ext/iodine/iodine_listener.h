#ifndef H___IODINE_LISTENER___H
#define H___IODINE_LISTENER___H
#include "iodine.h"

/* *****************************************************************************
Ruby Object.
***************************************************************************** */
static VALUE iodine_rb_IODINE_LISTENER;

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
    iodine_handler_method_injection__inner(iodine_rb_IODINE_LISTENER,          \
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
  VALUE r = iodine_listener_alloc(iodine_rb_IODINE_LISTENER);
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

static VALUE iodine_listener_map(int argc, VALUE *argv, VALUE o) {
  iodine_listener_s *l = iodine_listener_ptr(o);
  fio_http_settings_s settings;
  VALUE url = Qnil;
  VALUE handler = Qnil;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_RB(url, 0, "url", 0),
                  IODINE_ARG_RB(handler, 0, "handler", 0));
  if (!l->listener)
    rb_raise(rb_eRuntimeError,
             "call to `map` can only be called on active listeners");
  if (l->is_http) {
    if (RB_TYPE_P(url, RUBY_T_SYMBOL))
      url = rb_sym2str(rb_sym2id(url));
    if (!IODINE_STORE_IS_SKIP(url))
      rb_check_type(url, RUBY_T_STRING);
    if (handler == Qnil) { /* read value */
      handler = (VALUE)(fio_http_route_settings(
                            (fio_http_listener_s *)(l->listener),
                            IODINE_STORE_IS_SKIP(url) ? "/" : RSTRING_PTR(url))
                            ->udata);
    } else { /* set value for HTTP router */
      if (!IODINE_STORE_IS_SKIP(handler)) {
        STORE.hold(handler);
        iodine_handler_method_injection__inner(iodine_rb_IODINE_LISTENER,
                                               handler,
                                               0);
      }
      settings =
          *fio_http_listener_settings((fio_http_listener_s *)l->listener);
      if (IODINE_STORE_IS_SKIP(handler))
        handler = (VALUE)settings.udata;
      settings.udata = (void *)handler;
      settings.public_folder = FIO_STR_INFO0;
      // TODO: test for a handler's public folder property?
      fio_http_route FIO_NOOP((fio_http_listener_s *)l->listener,
                              RSTRING_PTR(url),
                              settings);
    }
  } else { /* not HTTP, URLs are invalid. */
    if (!IODINE_STORE_IS_SKIP(url))
      rb_raise(rb_eRuntimeError,
               "URL values are only valid for HTTP listener objects.");
    if (handler == Qnil) { /* read value */
      handler = iodine___listener_handler(l);
    } else { /* set value for raw router */
      fio_io_listener_udata_set((fio_io_listener_s *)(l->listener),
                                (void *)handler);
      iodine___listener_handler_set(l, handler);
    }
  }
  return handler;
}

static void iodine_io_http_on_http_resource(fio_http_s *h);
static VALUE iodine_handler_deafult_on_http404(VALUE handler, VALUE client);
static VALUE iodine_listener_map_resource(int argc, VALUE *argv, VALUE o) {
  iodine_listener_s *l = iodine_listener_ptr(o);
  fio_http_settings_s settings;
  VALUE url = Qnil;
  VALUE handler = Qnil;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_RB(url, 0, "url", 0),
                  IODINE_ARG_RB(handler, 0, "handler", 0));
  if (!l->listener)
    rb_raise(rb_eRuntimeError,
             "call to `map_resource` can only be called on active listeners.");
  if (!l->is_http)
    rb_raise(rb_eRuntimeError,
             "call to `map_resource` can only be called on HTTP listeners.");
  if (IODINE_STORE_IS_SKIP(url))
    rb_raise(rb_eRuntimeError,
             "call to `map_resource` can't be called on the root path.");

  if (RB_TYPE_P(url, RUBY_T_SYMBOL))
    url = rb_sym2str(rb_sym2id(url));
  rb_check_type(url, RUBY_T_STRING);

  if (handler == Qnil) { /* read value */
    handler = (VALUE)(fio_http_route_settings(
                          (fio_http_listener_s *)(l->listener),
                          IODINE_STORE_IS_SKIP(url) ? "/" : RSTRING_PTR(url))
                          ->udata);
  } else { /* set value for HTTP router */
    if (!IODINE_STORE_IS_SKIP(handler)) {
      STORE.hold(handler);
      iodine_handler_method_injection__inner(iodine_rb_IODINE_LISTENER,
                                             handler,
                                             0);
#define IODINE_DEFINE_MISSING_CALLBACK(id)                                     \
  do {                                                                         \
    if (!rb_respond_to(handler, id))                                           \
      rb_define_singleton_method(handler,                                      \
                                 rb_id2name(id),                               \
                                 iodine_handler_deafult_on_http404,            \
                                 1);                                           \
  } while (0)

      IODINE_DEFINE_MISSING_CALLBACK(IODINE_INDEX_ID);
      IODINE_DEFINE_MISSING_CALLBACK(IODINE_SHOW_ID);
      IODINE_DEFINE_MISSING_CALLBACK(IODINE_NEW_ID);
      IODINE_DEFINE_MISSING_CALLBACK(IODINE_EDIT_ID);
      IODINE_DEFINE_MISSING_CALLBACK(IODINE_CREATE_ID);
      IODINE_DEFINE_MISSING_CALLBACK(IODINE_UPDATE_ID);
      IODINE_DEFINE_MISSING_CALLBACK(IODINE_DELETE_ID);

#undef IODINE_DEFINE_MISSING_CALLBACK
    }
    settings = *fio_http_listener_settings((fio_http_listener_s *)l->listener);
    if (IODINE_STORE_IS_SKIP(handler))
      handler = (VALUE)settings.udata;
    settings.udata = (void *)handler;
    settings.public_folder = FIO_STR_INFO0;
    settings.on_http = iodine_io_http_on_http_resource;
    // TODO: test for a handler's public folder property?
    fio_http_route FIO_NOOP((fio_http_listener_s *)l->listener,
                            RSTRING_PTR(url),
                            settings);
  }
  return handler;
}
static VALUE iodine_listener_initialize(VALUE o) {
  rb_raise(rb_eRuntimeError,
           "Iodine Listeners can only be created using Iodine.listen");
  return Qnil;
}

/* *****************************************************************************
Initialize
***************************************************************************** */
static void Init_Iodine_Listener(void) {
  VALUE m = iodine_rb_IODINE_LISTENER =
      rb_define_class_under(iodine_rb_IODINE, "Listener", rb_cObject);
  STORE.hold(iodine_rb_IODINE_LISTENER);
  rb_define_alloc_func(m, iodine_listener_alloc);
  rb_define_method(m, "initialize", iodine_listener_initialize, 0);
  rb_define_method(m, "map", iodine_listener_map, -1);
  rb_define_method(m, "map_resource", iodine_listener_map_resource, -1);
}
#endif /* H___IODINE_LISTENER___H */
