#ifndef H___IODINE_TLS___
#define H___IODINE_TLS___
#include "iodine.h"
/* *****************************************************************************
TLS Wrapper
***************************************************************************** */

static void iodine_tls_free(void *ptr_) {
  fio_tls_s **p = (fio_tls_s **)ptr_;
  fio_tls_free(*p);
}

static VALUE iodine_tls_alloc(VALUE klass) {
  fio_tls_s **tls;
  VALUE o = Data_Make_Struct(klass, fio_tls_s *, NULL, iodine_tls_free, tls);
  *tls = fio_tls_new();
  return o;
}

static fio_tls_s *iodine_tls_get(VALUE self) {
  fio_tls_s **p;
  Data_Get_Struct(self, fio_tls_s *, p);
  return *p;
}

/* *****************************************************************************
The Functions to be Wrapped
***************************************************************************** */

#if 0
/**
 * Adds a certificate a new SSL/TLS context / settings object (SNI support).
 *
 *      fio_tls_cert_add(tls, "www.example.com",
 *                            "public_key.pem",
 *                            "private_key.pem", NULL );
 *
 * NOTE: Except for the `tls` and `server_name` arguments, all arguments might
 * be `NULL`, which a context builder (`fio_io_functions_s`) should treat as a
 * request for a self-signed certificate. It may be silently ignored.
 */
SFUNC fio_tls_s *fio_tls_cert_add(fio_tls_s *,
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
SFUNC fio_tls_s *fio_tls_alpn_add(fio_tls_s *tls,
                                  const char *protocol_name,
                                  void (*on_selected)(fio_s *));

/** Calls the `on_selected` callback for the `fio_tls_s` object. */
SFUNC int fio_tls_alpn_select(fio_tls_s *tls,
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
 * Note: when the `fio_tls_s` object is used for server connections, this should
 * limit connections to clients that connect using a trusted certificate.
 *
 *      fio_tls_trust_add(tls, "google-ca.pem" );
 */
SFUNC fio_tls_s *fio_tls_trust_add(fio_tls_s *, const char *public_cert_file);

/**
 * Returns the number of `fio_tls_cert_add` instructions.
 *
 * This could be used when deciding if to add a NULL instruction (self-signed).
 *
 * If `fio_tls_cert_add` was never called, zero (0) is returned.
 */
SFUNC uintptr_t fio_tls_cert_count(fio_tls_s *tls);

/**
 * Returns the number of registered ALPN protocol names.
 *
 * This could be used when deciding if protocol selection should be delegated to
 * the ALPN mechanism, or whether a protocol should be immediately assigned.
 *
 * If no ALPN protocols are registered, zero (0) is returned.
 */
SFUNC uintptr_t fio_tls_alpn_count(fio_tls_s *tls);

/**
 * Returns the number of `fio_tls_trust_add` instructions.
 *
 * This could be used when deciding if to disable peer verification or not.
 *
 * If `fio_tls_trust_add` was never called, zero (0) is returned.
 */
SFUNC uintptr_t fio_tls_trust_count(fio_tls_s *tls);

/** Arguments (and info) for `fio_tls_each`. */
typedef struct fio_tls_each_s {
  fio_tls_s *tls;
  void *udata;
  void *udata2;
  int (*each_cert)(struct fio_tls_each_s *,
                   const char *server_name,
                   const char *public_cert_file,
                   const char *private_key_file,
                   const char *pk_password);
  int (*each_alpn)(struct fio_tls_each_s *,
                   const char *protocol_name,
                   void (*on_selected)(fio_s *));
  int (*each_trust)(struct fio_tls_each_s *, const char *public_cert_file);
} fio_tls_each_s;

/** Calls callbacks for certificate, trust certificate and ALPN added. */
SFUNC int fio_tls_each(fio_tls_each_s);

/** `fio_tls_each` helper macro, see `fio_tls_each_s` for named arguments. */
#define fio_tls_each(tls_, ...)                                                \
  fio_tls_each(((fio_tls_each_s){.tls = tls_, __VA_ARGS__}))

/** If `NULL` returns current default, otherwise sets it. */
SFUNC fio_io_functions_s fio_tls_default_io_functions(fio_io_functions_s *);
#endif

/* *****************************************************************************
Wrapper API
***************************************************************************** */
static VALUE iodine_tls_cert_add(int argc, VALUE *argv, VALUE self) {
  fio_tls_s *tls = iodine_tls_get(self);
  VALUE server_name_rb;
  VALUE public_cert_file_rb;
  VALUE private_key_file_rb;
  VALUE pk_password_rb;
  char *server_name = NULL;
  char *public_cert_file = NULL;
  char *private_key_file = NULL;
  char *pk_password = NULL;
  fio_rb_multi_arg(argc,
                   argv,
                   FIO_RB_ARG(server_name_rb, 0, "name", Qnil, 0),
                   FIO_RB_ARG(public_cert_file_rb, 0, "cert", Qnil, 0),
                   FIO_RB_ARG(private_key_file_rb, 0, "key", Qnil, 0),
                   FIO_RB_ARG(pk_password_rb, 0, "password", Qnil, 0));
  if (server_name_rb != Qnil) {
    rb_check_type(server_name_rb, RUBY_T_STRING);
    server_name = RSTRING_PTR(server_name_rb);
  }
  if (public_cert_file_rb != Qnil) {
    rb_check_type(public_cert_file_rb, RUBY_T_STRING);
    public_cert_file = RSTRING_PTR(public_cert_file_rb);
  }
  if (private_key_file_rb != Qnil) {
    rb_check_type(private_key_file_rb, RUBY_T_STRING);
    private_key_file = RSTRING_PTR(private_key_file_rb);
  }
  if (pk_password_rb != Qnil) {
    rb_check_type(pk_password_rb, RUBY_T_STRING);
    pk_password = RSTRING_PTR(pk_password_rb);
  }
  fio_tls_cert_add(tls,
                   server_name,
                   public_cert_file,
                   private_key_file,
                   pk_password);
  return self;
}

/* *****************************************************************************
TLS Object Initialization
***************************************************************************** */
static void iodine_tls_init(void) {
  VALUE m = rb_define_class_under(iodine_rb_IODINE, "TLS", rb_cObject);
  rb_define_alloc_func(m, iodine_tls_alloc);
}

#endif
