#ifndef H___IODINE_TLS___H
#define H___IODINE_TLS___H
#include "iodine.h"
/* *****************************************************************************
TLS Wrapper
***************************************************************************** */

static void iodine_tls_free(void *ptr_) {
  fio_io_tls_s **p = (fio_io_tls_s **)ptr_;
  fio_io_tls_free(*p);
}

static VALUE iodine_tls_alloc(VALUE klass) {
  fio_io_tls_s **tls;
  VALUE o = Data_Make_Struct(klass, fio_io_tls_s *, NULL, iodine_tls_free, tls);
  *tls = fio_io_tls_new();
  return o;
}

static fio_io_tls_s *iodine_tls_get(VALUE self) {
  fio_io_tls_s **p;
  Data_Get_Struct(self, fio_io_tls_s *, p);
  return *p;
}

/* *****************************************************************************
The Functions to be Wrapped
***************************************************************************** */

#if 0
/**
 * Adds a certificate a new SSL/TLS context / settings object (SNI support).
 *
 *      fio_io_tls_cert_add(tls, "www.example.com",
 *                            "public_key.pem",
 *                            "private_key.pem", NULL );
 *
 * NOTE: Except for the `tls` and `server_name` arguments, all arguments might
 * be `NULL`, which a context builder (`fio_io_functions_s`) should treat as a
 * request for a self-signed certificate. It may be silently ignored.
 */
SFUNC fio_io_tls_s *fio_io_tls_cert_add(fio_io_tls_s *,
                                  const char *server_name,
                                  const char *public_cert_file,
                                  const char *private_key_file,
                                  const char *pk_password);

/**
 * Adds an ALPN protocol callback to the SSL/TLS context.
 *
 * The first protocol added will act as the default protocol to be selected.
 *
 * A `NULL` protocol name will be silently ignored.
 *
 * A `NULL` callback (`on_selected`) will be silently replaced with a no-op.
 */
SFUNC fio_io_tls_s *fio_io_tls_alpn_add(fio_io_tls_s *tls,
                                  const char *protocol_name,
                                  void (*on_selected)(fio_s *));

/** Calls the `on_selected` callback for the `fio_io_tls_s` object. */
SFUNC int fio_io_tls_alpn_select(fio_io_tls_s *tls,
                              const char *protocol_name,
                              size_t name_length,
                              fio_s *);

/**
 * Adds a certificate to the "trust" list, which automatically adds a peer
 * verification requirement.
 *
 * If `public_cert_file` is `NULL`, implementation is expected to add the
 * system's default trust registry.
 *
 * Note: when the `fio_io_tls_s` object is used for server connections, this should
 * limit connections to clients that connect using a trusted certificate.
 *
 *      fio_io_tls_trust_add(tls, "google-ca.pem" );
 */
SFUNC fio_io_tls_s *fio_io_tls_trust_add(fio_io_tls_s *, const char *public_cert_file);

/**
 * Returns the number of `fio_io_tls_cert_add` instructions.
 *
 * This could be used when deciding if to add a NULL instruction (self-signed).
 *
 * If `fio_io_tls_cert_add` was never called, zero (0) is returned.
 */
SFUNC uintptr_t fio_io_tls_cert_count(fio_io_tls_s *tls);

/**
 * Returns the number of registered ALPN protocol names.
 *
 * This could be used when deciding if protocol selection should be delegated to
 * the ALPN mechanism, or whether a protocol should be immediately assigned.
 *
 * If no ALPN protocols are registered, zero (0) is returned.
 */
SFUNC uintptr_t fio_io_tls_alpn_count(fio_io_tls_s *tls);

/**
 * Returns the number of `fio_io_tls_trust_add` instructions.
 *
 * This could be used when deciding if to disable peer verification or not.
 *
 * If `fio_io_tls_trust_add` was never called, zero (0) is returned.
 */
SFUNC uintptr_t fio_io_tls_trust_count(fio_io_tls_s *tls);

/** Arguments (and info) for `fio_io_tls_each`. */
typedef struct fio_io_tls_each_s {
  fio_io_tls_s *tls;
  void *udata;
  void *udata2;
  int (*each_cert)(struct fio_io_tls_each_s *,
                   const char *server_name,
                   const char *public_cert_file,
                   const char *private_key_file,
                   const char *pk_password);
  int (*each_alpn)(struct fio_io_tls_each_s *,
                   const char *protocol_name,
                   void (*on_selected)(fio_s *));
  int (*each_trust)(struct fio_io_tls_each_s *, const char *public_cert_file);
} fio_io_tls_each_s;

/** Calls callbacks for certificate, trust certificate and ALPN added. */
SFUNC int fio_io_tls_each(fio_io_tls_each_s);

/** `fio_io_tls_each` helper macro, see `fio_io_tls_each_s` for named arguments. */
#define fio_io_tls_each(tls_, ...)                                             \
  fio_io_tls_each(((fio_io_tls_each_s){.tls = tls_, __VA_ARGS__}))

/** If `NULL` returns current default, otherwise sets it. */
SFUNC fio_io_functions_s fio_io_tls_default_io_functions(fio_io_functions_s *);
#endif

/* *****************************************************************************
Wrapper API
***************************************************************************** */

// clang-format off
/**
Assigns the TLS context a public certificate, allowing remote parties to
validate the connection's identity.

A self signed certificate is automatically created if the `name` argument
is specified and either (or both) of the `cert` (public certificate) or `key`
(private key) arguments are missing.

Some implementations allow servers to have more than a single certificate, which
will be selected using the SNI extension. I believe the existing OpenSSL
implementation supports this option (untested).

     Iodine::TLS#add_cert(name = nil,
                          cert = nil,
                          key = nil,
                          password = nil)

Certificates and keys should be String objects leading to a PEM file.

This method also accepts named arguments. i.e.:

     tls = Iodine::TLS.new
     tls.add_cert name: "example.com"
     tls.add_cert cert: "my_cert.pem", key: "my_key.pem"
     tls.add_cert cert: "my_cert.pem", key: "my_key.pem", password: ENV['TLS_PASS']

Since TLS setup is crucial for security, an initialization error will result in
Iodine crashing with an error message. This is expected behavior.
*/ // clang-format on
static VALUE iodine_tls_cert_add(int argc, VALUE *argv, VALUE self) {
  fio_io_tls_s *tls = iodine_tls_get(self);
  fio_buf_info_s server_name = FIO_BUF_INFO1((char *)"localhost");
  fio_buf_info_s public_cert_file = FIO_BUF_INFO0;
  fio_buf_info_s private_key_file = FIO_BUF_INFO0;
  fio_buf_info_s pk_password = FIO_BUF_INFO0;
  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(server_name, 0, "name", 0),
                  IODINE_ARG_BUF(public_cert_file, 0, "cert", 0),
                  IODINE_ARG_BUF(private_key_file, 0, "key", 0),
                  IODINE_ARG_BUF(pk_password, 0, "password", 0));
  fio_io_tls_cert_add(tls,
                      server_name.buf,
                      public_cert_file.buf,
                      private_key_file.buf,
                      pk_password.buf);
  return self;
}

/** @deprecated use {Iodine::TLS.add_cert}. */
static VALUE iodine_tls_cert_add_old_name(int argc, VALUE *argv, VALUE self) {
  return iodine_tls_cert_add(argc, argv, self);
}

static void Init_Iodine_TLS(void) { /** Initialize Iodine::TLS */
  /** Used to setup a TLS contexts for connections (incoming / outgoing). */
  VALUE m = rb_define_class_under(iodine_rb_IODINE, "TLS", rb_cObject);
  rb_define_alloc_func(m, iodine_tls_alloc);
  rb_define_method(m, "add_cert", iodine_tls_cert_add, -1);
  rb_define_method(m, "use_certificate", iodine_tls_cert_add_old_name, -1);
#if HAVE_OPENSSL
  rb_const_set(m, rb_intern("SUPPORTED"), Qtrue);
#else
  rb_const_set(m, rb_intern("SUPPORTED"), Qfalse);
#endif
}

#endif /* H___IODINE_TLS___H */
