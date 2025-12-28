#ifndef H___IODINE_LISTENER___H
#define H___IODINE_LISTENER___H
#include "iodine.h"

/* *****************************************************************************
Iodine Listener - Server Socket Listener Management

This module provides the Iodine::Listener Ruby class which represents an
active server socket listener. Listeners are created via Iodine.listen()
and can be used to:

- Map URL routes to different handlers (for HTTP listeners)
- Get/set the handler for raw TCP/WebSocket connections
- Manage listener lifecycle

The Listener class wraps either:
- fio_http_listener_s for HTTP/WebSocket listeners
- fio_io_listener_s for raw TCP listeners

Ruby API (Iodine::Listener):
- listener.map(url: "/path", handler: obj) - Map URL to handler (HTTP only)
- listener.map                             - Get current handler

Note: Listeners can only be created via Iodine.listen(), not directly
instantiated.
***************************************************************************** */

/* *****************************************************************************
Ruby Object
***************************************************************************** */

/** The Iodine::Listener Ruby class */
static VALUE iodine_rb_IODINE_LISTENER;

/**
 * Internal structure representing an Iodine listener.
 *
 * Wraps either an HTTP listener (fio_http_listener_s) or a raw IO listener
 * (fio_io_listener_s) along with its Ruby handler object.
 */
typedef struct {
  void *listener;   /**< Pointer to fio_http_listener_s or fio_io_listener_s */
  VALUE handler;    /**< Ruby handler object for callbacks */
  bool is_http;     /**< true if HTTP listener, false if raw TCP */
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
Helpers - Internal Handler Management
***************************************************************************** */

/**
 * Macro called when a listener's handler is set.
 * Releases the old handler from the store and holds the new one.
 * Also injects handler methods into the listener class.
 */
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

/**
 * Creates a new Iodine::Listener Ruby object wrapping a native listener.
 *
 * This is called internally by Iodine.listen() to create the Ruby wrapper.
 *
 * @param listener Pointer to the native listener (HTTP or raw)
 * @param handler Ruby handler object for callbacks
 * @param is_http true for HTTP listeners, false for raw TCP
 * @return New Iodine::Listener VALUE
 *
 * @note Raises NoMemError if allocation fails.
 */
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
API - Ruby Methods
***************************************************************************** */

/**
 * Maps a URL path to a handler or retrieves the current handler.
 *
 * For HTTP listeners:
 * - With url and handler: Maps the URL path to the handler
 * - With url only: Returns the handler for that URL path
 * - With neither: Returns the default handler
 *
 * For raw TCP listeners:
 * - URL parameter is not allowed (raises RuntimeError)
 * - With handler: Sets the connection handler
 * - Without handler: Returns the current handler
 *
 * @param argc Number of arguments
 * @param argv Array of arguments (url:, handler:)
 * @param o The Iodine::Listener instance
 * @return The handler for the specified URL/listener
 *
 * @note Raises RuntimeError if called on an inactive listener.
 * @note Raises RuntimeError if URL is provided for non-HTTP listener.
 *
 * Ruby: listener.map(url: "/api", handler: MyHandler)
 *       listener.map(url: "/api")  # => returns handler
 *       listener.map               # => returns default handler
 */
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

/**
 * Raises an error - Listeners cannot be directly instantiated.
 *
 * Listeners must be created via Iodine.listen() which returns a
 * properly configured Listener object.
 *
 * @param o The Listener instance (unused)
 * @return Never returns (always raises)
 *
 * @note Always raises RuntimeError.
 */
static VALUE iodine_listener_initialize(VALUE o) {
  rb_raise(rb_eRuntimeError,
           "Iodine Listeners can only be created using Iodine.listen");
  return Qnil;
}

/* *****************************************************************************
Initialize - Ruby Class Registration
***************************************************************************** */

/**
 * Initializes the Iodine::Listener Ruby class.
 *
 * Defines the class under the Iodine module and registers:
 * - Allocator function
 * - initialize method (raises error - use Iodine.listen instead)
 * - map method for URL routing (HTTP) or handler access (raw)
 */
static void Init_Iodine_Listener(void) {
  VALUE m = iodine_rb_IODINE_LISTENER =
      rb_define_class_under(iodine_rb_IODINE, "Listener", rb_cObject);
  STORE.hold(iodine_rb_IODINE_LISTENER);
  rb_define_alloc_func(m, iodine_listener_alloc);
  rb_define_method(m, "initialize", iodine_listener_initialize, 0);
  rb_define_method(m, "map", iodine_listener_map, -1);
}
#endif /* H___IODINE_LISTENER___H */
