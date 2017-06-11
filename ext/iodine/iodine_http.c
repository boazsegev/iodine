/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine_http.h"
#include "rb-rack-io.h"

/* *****************************************************************************
Available Globals
***************************************************************************** */
VALUE IodineHTTP;

typedef struct {
  VALUE app;
  unsigned long max_msg : 56;
  unsigned ping : 8;
} iodine_http_settings_s;

/* these three are used also by rb-rack-io.c */
VALUE R_HIJACK;
VALUE R_HIJACK_IO;
VALUE R_HIJACK_CB;

VALUE UPGRADE_TCP;
VALUE UPGRADE_TCP_Q;
VALUE UPGRADE_WEBSOCKET;
VALUE UPGRADE_WEBSOCKET_Q;
/* backwards compatibility, temp */
VALUE IODINE_UPGRADE;
VALUE IODINE_WEBSOCKET;

static VALUE hijack_func_sym;
static ID to_fixnum_func_id;
static ID close_method_id;
static ID each_method_id;

#define rack_declare(rack_name) static VALUE rack_name

#define rack_set(rack_name, str)                                               \
  (rack_name) = rb_enc_str_new((str), strlen((str)), BinaryEncoding);          \
  rb_global_variable(&(rack_name));                                            \
  rb_obj_freeze(rack_name);

#define rack_autoset(rack_name) rack_set((rack_name), #rack_name)

static uint8_t IODINE_IS_DEVELOPMENT_MODE = 0;

static VALUE ENV_TEMPLATE;

rack_declare(HTTP_SCHEME);
rack_declare(HTTPS_SCHEME);
rack_declare(QUERY_ESTRING);
rack_declare(REQUEST_METHOD);
rack_declare(PATH_INFO);
rack_declare(QUERY_STRING);
rack_declare(QUERY_ESTRING);
rack_declare(SERVER_NAME);
rack_declare(SERVER_PORT);
rack_declare(SERVER_PROTOCOL);
rack_declare(HTTP_VERSION);
rack_declare(REMOTE_ADDR);
rack_declare(CONTENT_LENGTH);
rack_declare(CONTENT_TYPE);
rack_declare(R_URL_SCHEME);          // rack.url_scheme
rack_declare(R_INPUT);               // rack.input
rack_declare(XSENDFILE);             // for X-Sendfile support
rack_declare(CONTENT_LENGTH_HEADER); // for X-Sendfile support
// rack_declare(R_HIJACK); // rack.hijack
// rack_declare(R_HIJACK_CB);// rack.hijack_io

/* *****************************************************************************
Handling HTTP requests
***************************************************************************** */

/* *****************************************************************************
Listenninng to HTTP
***************************************************************************** */

static void iodine_print_http_msg(void *public_folder_, void *arg2) {
  (void)arg2;
  if (defer_fork_pid())
    return;
  // Write message
  VALUE iodine_version = rb_const_get(Iodine, rb_intern("VERSION"));
  VALUE ruby_version = rb_const_get(Iodine, rb_intern("RUBY_VERSION"));
  VALUE public_folder = (VALUE)public_folder_;
  if (public_folder) {
    fprintf(stderr,
            "Starting up Iodine HTTP Server:\n"
            " * Ruby v.%s\n * Iodine v.%s \n"
            " * %lu max concurrent connections / open files\n"
            " * Serving static files from %s",
            StringValueCStr(ruby_version), StringValueCStr(iodine_version),
            (size_t)sock_max_capacity(), StringValueCStr(public_folder));
    Registry.remove(public_folder);
  } else
    fprintf(stderr,
            "Starting up Iodine HTTP Server:\n"
            " * Ruby v.%s\n * Iodine v.%s \n"
            " * %lu max concurrent connections / open files\n"
            "\n",
            StringValueCStr(ruby_version), StringValueCStr(iodine_version),
            (size_t)sock_max_capacity());
}

static void free_iodine_http(void *set_, void *ignr) {
  iodine_http_settings_s *set = set_;
  Registry.remove(set->app);
  free(set);
  (void)ignr;
}
/**
Listens to incoming HTTP connections and handles incoming requests using the
Rack specification.

This is delegated to a lower level C HTTP and Websocket implementation, no Ruby
object will be crated except the `env` object required by the Rack
specifications.

Accepts a single Hash argument with the following properties:

`app`:: the Rack application that handles incoming requests. Default: `nil`.
`port`:: the port to listen to. Default: 3000.
`address`:: the address to bind to. Default: binds to all possible addresses.
`log`:: enable response logging (Hijacked sockets aren't logged). Default: off.
`public`:: The root public folder for static file service. Default: none.
`timeout`:: Timeout for inactive HTTP/1.x connections. Defaults: 5 seconds.
`max_body`:: The maximum body size for incoming HTTP messages. Default: ~50Mib.
`max_msg`:: The maximum Websocket message size allowed. Default: ~250Kib.
`ping`:: The Websocket `ping` interval. Default: 40 sec.

Either the `app` or the `public` properties are required. If niether exists, the
function will fail. If both exist, Iodine will serve static files as well as
dynamic requests.

When using the static file server, it's possible to serve `gzip` versions of the
static files by saving a compressed version with the `gz` extension (i.e.
`styles.css.gz`).

`gzip` will only be served to clients tat support the `gzip` transfer encoding.

Once HTTP/2 is supported (planned, but probably very far away), HTTP/2 timeouts
will be dynamically managed by Iodine. The `timeout` option is only relevant to
HTTP/1.x connections.
*/
VALUE iodine_http_listen(VALUE self, VALUE opt) {
  uint8_t log_http = 0;
  size_t ping = 0;
  size_t max_body = 0;
  size_t max_msg = 0;
  Check_Type(opt, T_HASH);
  VALUE app = rb_hash_aref(opt, rb_str_new_cstr("app"));
  VALUE www = rb_hash_aref(opt, rb_str_new_cstr("public"));
  VALUE port = rb_hash_aref(opt, rb_str_new_cstr("port"));
  VALUE address = rb_hash_aref(opt, rb_str_new_cstr("address"));
  VALUE tout = rb_hash_aref(opt, rb_str_new_cstr("timeout"));

  VALUE tmp = rb_hash_aref(opt, rb_str_new_cstr("max_msg"));
  if (tmp != Qnil && tmp != Qfalse) {
    Check_Type(tmp, T_FIXNUM);
    max_msg = FIX2ULONG(tmp);
  }

  tmp = rb_hash_aref(opt, rb_str_new_cstr("max_body"));
  if (tmp != Qnil && tmp != Qfalse) {
    Check_Type(tmp, T_FIXNUM);
    max_body = FIX2ULONG(tmp);
  }

  tmp = rb_hash_aref(opt, rb_str_new_cstr("ping"));
  if (tmp != Qnil && tmp != Qfalse) {
    Check_Type(tmp, T_FIXNUM);
    ping = FIX2ULONG(tmp);
  }
  if (ping > 255) {
    fprintf(stderr, "Iodine Warning: Websocket timeout value "
                    "is over 255 and is silently ignored.\n");
    ping = 0;
  }

  tmp = rb_hash_aref(opt, rb_str_new_cstr("log"));
  if (tmp != Qnil && tmp != Qfalse)
    log_http = 1;

  if ((app == Qnil || app == Qfalse) && (www == Qnil || www == Qfalse))
    return Qfalse;

  if ((www != Qnil && www != Qfalse)) {
    Check_Type(www, T_STRING);
    Registry.add(www);
  } else
    www = 0;

  if ((address != Qnil && address != Qfalse))
    Check_Type(address, T_STRING);
  else
    address = 0;

  if ((tout != Qnil && tout != Qfalse)) {
    Check_Type(tout, T_FIXNUM);
    tout = FIX2ULONG(tout);
  } else
    tout = 0;
  if (tout > 255) {
    fprintf(stderr, "Iodine Warning: HTTP timeout value "
                    "is over 255 and is silently ignored.\n");
    tout = 0;
  }

  if (port != Qnil && port != Qfalse) {
    if (!RB_TYPE_P(port, T_STRING) || !RB_TYPE_P(port, T_FIXNUM))
      rb_raise(rb_eTypeError,
               "The `port` property MUST be either a String or a Number");
    if (RB_TYPE_P(port, T_FIXNUM))
      port = RubyCaller.call(port, to_s_method_id);
  } else
    port = 0;

  if ((app != Qnil && app != Qfalse))
    Registry.add(app);
  else
    app = 0;
  iodine_http_settings_s *set = malloc(sizeof(*set));
  *set = (iodine_http_settings_s){.app = app, .ping = ping, .max_msg = max_msg};

  if (http_listen((port ? StringValueCStr(port) : "3000"),
                  (address ? StringValueCStr(address) : NULL), .udata = set,
                  .timeout = (tout ? FIX2INT(tout) : tout),
                  .on_finish = free_iodine_http, .log_static = log_http,
                  .max_body_size = max_body))
    return Qfalse;

  defer(iodine_print_http_msg, (www ? (void *)www : NULL), NULL);
  return Qtrue;
  (void)self;
}

/* *****************************************************************************
Initialization
*****************************************************************************
*/

void Iodine_init_http(void) {

  IodineHTTP = rb_define_module_under(Iodine, "HTTP");

  rb_define_module_function(Iodine, "listen", iodine_http_listen, 1);

  rack_autoset(REQUEST_METHOD);
  rack_autoset(PATH_INFO);
  rack_autoset(QUERY_STRING);
  rack_autoset(SERVER_NAME);
  rack_autoset(SERVER_PORT);
  rack_autoset(CONTENT_LENGTH);
  rack_autoset(CONTENT_TYPE);
  rack_autoset(SERVER_PROTOCOL);
  rack_autoset(HTTP_VERSION);
  rack_autoset(REMOTE_ADDR);
  rack_set(HTTP_SCHEME, "http");
  rack_set(HTTPS_SCHEME, "https");
  rack_set(QUERY_ESTRING, "");
  rack_set(R_URL_SCHEME, "rack.url_scheme");
  rack_set(R_INPUT, "rack.input");
  rack_set(XSENDFILE, "X-Sendfile");
  rack_set(CONTENT_LENGTH_HEADER, "Content-Length");

  rack_set(R_HIJACK_IO, "rack.hijack_io");
  rack_set(R_HIJACK, "rack.hijack");
  rack_set(R_HIJACK_CB, "iodine.hijack_cb");

  rack_set(UPGRADE_TCP, "upgrade.tcp");
  rack_set(UPGRADE_WEBSOCKET, "upgrade.websocket");

  rack_set(UPGRADE_TCP_Q, "upgrade.tcp?");
  rack_set(UPGRADE_WEBSOCKET_Q, "upgrade.websocket?");

  rack_set(QUERY_ESTRING, "");
  rack_set(QUERY_ESTRING, "");

  hijack_func_sym = ID2SYM(rb_intern("_hijack"));
  close_method_id = rb_intern("close");
  each_method_id = rb_intern("each");

  RackIO.init();
}
