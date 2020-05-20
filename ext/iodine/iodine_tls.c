#include <ruby.h>

#include <fio.h>
#include <fio_tls.h>
#include <iodine.h>

static VALUE server_name_sym = Qnil, certificate_sym = Qnil,
             private_key_sym = Qnil, password_sym = Qnil;
VALUE IodineTLSClass;
/* *****************************************************************************
C <=> Ruby Data allocation
***************************************************************************** */

static size_t iodine_tls_data_size(const void *c_) {
  return sizeof(fio_tls_s *);
  (void)c_;
}

static void iodine_tls_data_free(void *c_) { fio_tls_destroy(c_); }

static const rb_data_type_t iodine_tls_data_type = {
    .wrap_struct_name = "IodineTLSData",
    .function =
        {
            .dmark = NULL,
            .dfree = iodine_tls_data_free,
            .dsize = iodine_tls_data_size,
        },
    .data = NULL,
    // .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

/* Iodine::PubSub::Engine.allocate */
static VALUE iodine_tls_data_alloc_c(VALUE klass) {
  fio_tls_s *tls = fio_tls_new(NULL, NULL, NULL, NULL);
  FIO_ASSERT_ALLOC(tls);
  return TypedData_Wrap_Struct(klass, &iodine_tls_data_type, tls);
}

/* *****************************************************************************
ALPN selection callback
***************************************************************************** */

FIO_FUNC void iodine_tls_alpn_cb(intptr_t uuid, void *udata, void *block_) {
  if (!fio_is_valid(uuid)) {
    FIO_LOG_DEBUG("ALPN callback called for invalid connetion. SSL/TLS error?");
    return;
  }
  VALUE new_handler = IodineCaller.call((VALUE)block_, iodine_call_id);
  if (!new_handler || new_handler == Qnil || new_handler == Qtrue ||
      new_handler == Qfalse) {
    fio_close(uuid);
    return;
  }

  iodine_tcp_attch_uuid(uuid, new_handler);

  (void)udata; /* we can't use `udata`, since it's different in HTTP vs. TCP */
}

/* *****************************************************************************
C API
***************************************************************************** */

fio_tls_s *iodine_tls2c(VALUE self) {
  fio_tls_s *c = NULL;
  if (self == Qnil || self == Qfalse)
    return NULL;
  TypedData_Get_Struct(self, fio_tls_s, &iodine_tls_data_type, c);
  if (!c) {
    rb_raise(rb_eTypeError, "Iodine::TLS error - not an Iodine::TLS object?");
  }
  return c;
}

/* *****************************************************************************
Ruby API
***************************************************************************** */

/**
Assigns the TLS context a public sertificate, allowing remote parties to
validate the connection's identity.

A self signed certificate is automatically created if the `server_name` argument
is specified and either (or both) of the `certificate` or `private_ket`
arguments are missing.

Some implementations allow servers to have more than a single certificate, which
will be selected using the SNI extension. I believe the existing OpenSSL
implementation supports this option (untested).

     Iodine::TLS#use_certificate(server_name,
                                 certificate = nil,
                                 private_key = nil,
                                 password = nil)

Certificates and keys should be String objects leading to a PEM file.

This method also accepts named arguments. i.e.:

     tls = Iodine::TLS.new
     tls.use_certificate server_name: "example.com"
     tls.use_certificate certificate: "my_cert.pem", private_key: "my_key.pem"

Since TLS setup is crucial for security, a missing file will result in Iodine
crashing with an error message. This is expected behavior.

*/
static VALUE iodine_tls_use_certificate(int argc, VALUE *argv, VALUE self) {
  VALUE server_name = Qnil, certificate = Qnil, private_key = Qnil,
        password = Qnil;
  if (argc == 1 && RB_TYPE_P(argv[0], T_HASH)) {
    /* named arguments */
    server_name = rb_hash_aref(argv[0], server_name_sym);
    certificate = rb_hash_aref(argv[0], certificate_sym);
    private_key = rb_hash_aref(argv[0], private_key_sym);
    password = rb_hash_aref(argv[0], password_sym);
  } else {
    /* regular arguments */
    switch (argc) {
    case 4: /* overflow */
      password = argv[3];
      Check_Type(password, T_STRING);
    case 3: /* overflow */
      private_key = argv[1];
      Check_Type(private_key, T_STRING);
    case 2: /* overflow */
      certificate = argv[2];
      Check_Type(certificate, T_STRING);
    case 1: /* overflow */
      server_name = argv[0];
      Check_Type(server_name, T_STRING);
      break;
    default:
      rb_raise(rb_eArgError, "expecting 1..4 arguments or named arguments.");
      return self;
    }
  }

  fio_tls_s *t = iodine_tls2c(self);
  char *srvname = (server_name == Qnil ? NULL : StringValueCStr(server_name));
  char *pubcert = (certificate == Qnil ? NULL : StringValueCStr(certificate));
  char *prvkey = (private_key == Qnil ? NULL : StringValueCStr(private_key));
  char *pass = (password == Qnil ? NULL : StringValueCStr(password));

  fio_tls_cert_add(t, srvname, pubcert, prvkey, pass);
  return self;
}

/**
Adds a certificate PEM file to the list of trusted certificates and enforces
peer verification.

This is extremely important when using {Iodine::TLS} for client connections,
since adding the target server's

It is enough to add the Certificate Authority's (CA) certificate, there's no
need to add each client or server certificate.

When {trust} is used on a server TLS, only trusted clients will be allowed to
connect.

Since TLS setup is crucial for security, a missing file will result in Iodine
crashing with an error message. This is expected behavior.
*/
static VALUE iodine_tls_trust(VALUE self, VALUE certificate) {
  Check_Type(certificate, T_STRING);
  fio_tls_s *t = iodine_tls2c(self);
  char *pubcert =
      (certificate == Qnil ? NULL : IODINE_RSTRINFO(certificate).data);
  fio_tls_trust(t, pubcert);
  return self;
}

/**
Adds an ALPN protocol callback for the named protocol, the required block must
return the handler for that protocol.

The first protocol added will be the default protocol in cases where ALPN
failed.

i.e.:

     tls.on_protocol("http/1.1") { HTTPConnection.new }

When implementing TLS clients, this identifies the protocol(s) that should be
requested by the client.

When implementing TLS servers, this identifies the protocol(s) offered by the
server.

More than a single protocol can be set, but iodine doesn't offer, at this
moment, a way to handle these changes or to detect which protocol was selected
except by assigning a different callback per protocol.

This is implemented using the ALPN extension to TLS.
*/
static VALUE iodine_tls_alpn(VALUE self, VALUE protocol_name) {
  Check_Type(protocol_name, T_STRING);
  rb_need_block();
  fio_tls_s *t = iodine_tls2c(self);
  char *prname =
      (protocol_name == Qnil ? NULL : IODINE_RSTRINFO(protocol_name).data);
  VALUE block = IodineStore.add(rb_block_proc());
  fio_tls_alpn_add(t, prname, iodine_tls_alpn_cb, (void *)block,
                   (void (*)(void *))IodineStore.remove);
  return self;
}

/**
Creates a new {Iodine::TLS} object and calles the {#use_certificate} method with
the supplied arguments.
*/
static VALUE iodine_tls_new(int argc, VALUE *argv, VALUE self) {
  if (argc) {
    iodine_tls_use_certificate(argc, argv, self);
  }
  return self;
}

/* *****************************************************************************
Initialize Iodine::TLS
***************************************************************************** */
#define IODINE_MAKE_SYM(name)                                                  \
  do {                                                                         \
    name##_sym = rb_id2sym(rb_intern(#name));                                  \
    rb_global_variable(&name##_sym);                                           \
  } while (0)

void iodine_init_tls(void) {

  IODINE_MAKE_SYM(server_name);
  IODINE_MAKE_SYM(certificate);
  IODINE_MAKE_SYM(private_key);
  IODINE_MAKE_SYM(password);

  IodineTLSClass = rb_define_class_under(IodineModule, "TLS", rb_cData);
  rb_define_alloc_func(IodineTLSClass, iodine_tls_data_alloc_c);
  rb_define_method(IodineTLSClass, "initialize", iodine_tls_new, -1);
  rb_define_method(IodineTLSClass, "use_certificate",
                   iodine_tls_use_certificate, -1);
  rb_define_method(IodineTLSClass, "trust", iodine_tls_trust, 1);
  rb_define_method(IodineTLSClass, "on_protocol", iodine_tls_alpn, 1);

#if HAVE_OPENSSL
  rb_const_set(IodineTLSClass, rb_intern("SUPPORTED"), Qtrue);
#elif HAVE_BEARSSL
  rb_const_set(IodineTLSClass, rb_intern("SUPPORTED"), Qfalse);
#else
  rb_const_set(IodineTLSClass, rb_intern("SUPPORTED"), Qfalse);
#endif
}
#undef IODINE_MAKE_SYM
