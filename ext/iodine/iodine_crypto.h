#ifndef H___IODINE_CRYPTO___H
#define H___IODINE_CRYPTO___H
#include "iodine.h"

/* *****************************************************************************
Iodine::Base::Crypto - Advanced Cryptographic Operations

Provides access to modern cryptographic primitives:
- ChaCha20-Poly1305: AEAD symmetric encryption
- Ed25519: Digital signatures
- X25519: Key exchange and public-key encryption (ECIES)
- HKDF: Key derivation (RFC 5869)
***************************************************************************** */

static VALUE iodine_rb_CRYPTO;
static VALUE iodine_rb_CHACHA20POLY1305;
static VALUE iodine_rb_ED25519;
static VALUE iodine_rb_X25519;
static VALUE iodine_rb_HKDF;

/* *****************************************************************************
ChaCha20-Poly1305 AEAD Encryption
***************************************************************************** */

/**
 * Encrypts data using ChaCha20-Poly1305 AEAD.
 *
 * @param data [String] Plaintext to encrypt
 * @param key: [String] 32-byte encryption key
 * @param nonce: [String] 12-byte nonce (must be unique per key)
 * @param ad: [String, nil] Optional additional authenticated data
 * @return [Array<String, String>] [ciphertext, mac] where mac is 16 bytes
 */
FIO_SFUNC VALUE iodine_crypto_chacha_encrypt(int argc,
                                             VALUE *argv,
                                             VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  fio_buf_info_s key = FIO_BUF_INFO0;
  fio_buf_info_s nonce = FIO_BUF_INFO0;
  fio_buf_info_s ad = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_BUF(key, 0, "key", 1),
                  IODINE_ARG_BUF(nonce, 0, "nonce", 1),
                  IODINE_ARG_BUF(ad, 0, "ad", 0));

  if (key.len != 32)
    rb_raise(rb_eArgError, "key must be 32 bytes (got %zu)", key.len);
  if (nonce.len != 12)
    rb_raise(rb_eArgError, "nonce must be 12 bytes (got %zu)", nonce.len);

  /* Copy data for in-place encryption */
  VALUE ciphertext = rb_str_new(data.buf, data.len);
  uint8_t mac[16];

  fio_chacha20_poly1305_enc(mac,
                            RSTRING_PTR(ciphertext),
                            data.len,
                            ad.buf,
                            ad.len,
                            (void *)key.buf,
                            (void *)nonce.buf);

  VALUE mac_str = rb_str_new((const char *)mac, 16);
  return rb_ary_new_from_args(2, ciphertext, mac_str);
  (void)self;
}

/**
 * Decrypts data using ChaCha20-Poly1305 AEAD.
 *
 * @param ciphertext [String] Ciphertext to decrypt
 * @param mac: [String] 16-byte authentication tag
 * @param key: [String] 32-byte encryption key
 * @param nonce: [String] 12-byte nonce
 * @param ad: [String, nil] Optional additional authenticated data
 * @return [String] Decrypted plaintext
 * @raise [RuntimeError] if authentication fails
 */
FIO_SFUNC VALUE iodine_crypto_chacha_decrypt(int argc,
                                             VALUE *argv,
                                             VALUE self) {
  fio_buf_info_s data = FIO_BUF_INFO0;
  fio_buf_info_s mac = FIO_BUF_INFO0;
  fio_buf_info_s key = FIO_BUF_INFO0;
  fio_buf_info_s nonce = FIO_BUF_INFO0;
  fio_buf_info_s ad = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(data, 0, NULL, 1),
                  IODINE_ARG_BUF(mac, 0, "mac", 1),
                  IODINE_ARG_BUF(key, 0, "key", 1),
                  IODINE_ARG_BUF(nonce, 0, "nonce", 1),
                  IODINE_ARG_BUF(ad, 0, "ad", 0));

  if (key.len != 32)
    rb_raise(rb_eArgError, "key must be 32 bytes (got %zu)", key.len);
  if (nonce.len != 12)
    rb_raise(rb_eArgError, "nonce must be 12 bytes (got %zu)", nonce.len);
  if (mac.len != 16)
    rb_raise(rb_eArgError, "mac must be 16 bytes (got %zu)", mac.len);

  /* Copy data for in-place decryption, also copy mac since it's modified */
  VALUE plaintext = rb_str_new(data.buf, data.len);
  uint8_t mac_copy[16];
  FIO_MEMCPY(mac_copy, mac.buf, 16);

  int result = fio_chacha20_poly1305_dec(mac_copy,
                                         RSTRING_PTR(plaintext),
                                         data.len,
                                         ad.buf,
                                         ad.len,
                                         (void *)key.buf,
                                         (void *)nonce.buf);

  if (result != 0)
    rb_raise(rb_eRuntimeError, "Authentication failed");

  return plaintext;
  (void)self;
}

/* *****************************************************************************
Ed25519 Digital Signatures
***************************************************************************** */

/**
 * Generates a new Ed25519 key pair.
 *
 * @return [Array<String, String>] [secret_key, public_key] both 32 bytes
 */
FIO_SFUNC VALUE iodine_crypto_ed25519_keypair(VALUE self) {
  uint8_t sk[32], pk[32];
  fio_ed25519_keypair(sk, pk);
  VALUE secret = rb_str_new((const char *)sk, 32);
  VALUE public = rb_str_new((const char *)pk, 32);
  /* Clear secret key from stack */
  fio_memset(sk, 0, 32);
  return rb_ary_new_from_args(2, secret, public);
  (void)self;
}

/**
 * Derives the public key from an Ed25519 secret key.
 *
 * @param secret_key: [String] 32-byte secret key
 * @return [String] 32-byte public key
 */
FIO_SFUNC VALUE iodine_crypto_ed25519_public_key(int argc,
                                                 VALUE *argv,
                                                 VALUE self) {
  fio_buf_info_s sk = FIO_BUF_INFO0;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_BUF(sk, 0, "secret_key", 1));

  if (sk.len != 32)
    rb_raise(rb_eArgError, "secret_key must be 32 bytes (got %zu)", sk.len);

  uint8_t pk[32];
  fio_ed25519_public_key(pk, (const uint8_t *)sk.buf);
  return rb_str_new((const char *)pk, 32);
  (void)self;
}

/**
 * Signs a message using Ed25519.
 *
 * @param message [String] Message to sign
 * @param secret_key: [String] 32-byte secret key
 * @param public_key: [String] 32-byte public key
 * @return [String] 64-byte signature
 */
FIO_SFUNC VALUE iodine_crypto_ed25519_sign(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s message = FIO_BUF_INFO0;
  fio_buf_info_s sk = FIO_BUF_INFO0;
  fio_buf_info_s pk = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(message, 0, NULL, 1),
                  IODINE_ARG_BUF(sk, 0, "secret_key", 1),
                  IODINE_ARG_BUF(pk, 0, "public_key", 1));

  if (sk.len != 32)
    rb_raise(rb_eArgError, "secret_key must be 32 bytes (got %zu)", sk.len);
  if (pk.len != 32)
    rb_raise(rb_eArgError, "public_key must be 32 bytes (got %zu)", pk.len);

  uint8_t sig[64];
  fio_ed25519_sign(sig,
                   message.buf,
                   message.len,
                   (const uint8_t *)sk.buf,
                   (const uint8_t *)pk.buf);
  return rb_str_new((const char *)sig, 64);
  (void)self;
}

/**
 * Verifies an Ed25519 signature.
 *
 * @param signature [String] 64-byte signature
 * @param message [String] Original message
 * @param public_key: [String] 32-byte public key
 * @return [Boolean] true if valid, false otherwise
 */
FIO_SFUNC VALUE iodine_crypto_ed25519_verify(int argc,
                                             VALUE *argv,
                                             VALUE self) {
  fio_buf_info_s sig = FIO_BUF_INFO0;
  fio_buf_info_s message = FIO_BUF_INFO0;
  fio_buf_info_s pk = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(sig, 0, NULL, 1),
                  IODINE_ARG_BUF(message, 1, NULL, 1),
                  IODINE_ARG_BUF(pk, 0, "public_key", 1));

  if (sig.len != 64)
    rb_raise(rb_eArgError, "signature must be 64 bytes (got %zu)", sig.len);
  if (pk.len != 32)
    rb_raise(rb_eArgError, "public_key must be 32 bytes (got %zu)", pk.len);

  int result = fio_ed25519_verify((const uint8_t *)sig.buf,
                                  message.buf,
                                  message.len,
                                  (const uint8_t *)pk.buf);
  return result == 0 ? Qtrue : Qfalse;
  (void)self;
}

/* *****************************************************************************
X25519 Key Exchange
***************************************************************************** */

/**
 * Generates a new X25519 key pair.
 *
 * @return [Array<String, String>] [secret_key, public_key] both 32 bytes
 */
FIO_SFUNC VALUE iodine_crypto_x25519_keypair(VALUE self) {
  uint8_t sk[32], pk[32];
  fio_x25519_keypair(sk, pk);
  VALUE secret = rb_str_new((const char *)sk, 32);
  VALUE public = rb_str_new((const char *)pk, 32);
  /* Clear secret key from stack */
  fio_memset(sk, 0, 32);
  return rb_ary_new_from_args(2, secret, public);
  (void)self;
}

/**
 * Derives the public key from an X25519 secret key.
 *
 * @param secret_key: [String] 32-byte secret key
 * @return [String] 32-byte public key
 */
FIO_SFUNC VALUE iodine_crypto_x25519_public_key(int argc,
                                                VALUE *argv,
                                                VALUE self) {
  fio_buf_info_s sk = FIO_BUF_INFO0;
  iodine_rb2c_arg(argc, argv, IODINE_ARG_BUF(sk, 0, "secret_key", 1));

  if (sk.len != 32)
    rb_raise(rb_eArgError, "secret_key must be 32 bytes (got %zu)", sk.len);

  uint8_t pk[32];
  fio_x25519_public_key(pk, (const uint8_t *)sk.buf);
  return rb_str_new((const char *)pk, 32);
  (void)self;
}

/**
 * Computes a shared secret using X25519 (ECDH).
 *
 * Both parties compute the same shared secret:
 *   shared = X25519(my_secret, their_public)
 *
 * @param secret_key: [String] 32-byte own secret key
 * @param their_public: [String] 32-byte other party's public key
 * @return [String] 32-byte shared secret
 * @raise [RuntimeError] if key exchange fails (e.g., low-order point)
 */
FIO_SFUNC VALUE iodine_crypto_x25519_shared_secret(int argc,
                                                   VALUE *argv,
                                                   VALUE self) {
  fio_buf_info_s sk = FIO_BUF_INFO0;
  fio_buf_info_s their_pk = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(sk, 0, "secret_key", 1),
                  IODINE_ARG_BUF(their_pk, 0, "their_public", 1));

  if (sk.len != 32)
    rb_raise(rb_eArgError, "secret_key must be 32 bytes (got %zu)", sk.len);
  if (their_pk.len != 32)
    rb_raise(rb_eArgError,
             "their_public must be 32 bytes (got %zu)",
             their_pk.len);

  uint8_t shared[32];
  int result = fio_x25519_shared_secret(shared,
                                        (const uint8_t *)sk.buf,
                                        (const uint8_t *)their_pk.buf);
  if (result != 0)
    rb_raise(rb_eRuntimeError, "Key exchange failed (invalid public key)");

  return rb_str_new((const char *)shared, 32);
  (void)self;
}

/**
 * Encrypts a message using X25519 public-key encryption (ECIES).
 *
 * Uses ephemeral key agreement + ChaCha20-Poly1305 for authenticated
 * encryption. Only the recipient with the matching secret key can decrypt.
 *
 * @param message [String] Plaintext to encrypt
 * @param recipient_pk: [String] 32-byte recipient's public key
 * @return [String] Ciphertext (message.length + 48 bytes overhead)
 * @raise [RuntimeError] if encryption fails
 */
FIO_SFUNC VALUE iodine_crypto_x25519_encrypt(int argc,
                                             VALUE *argv,
                                             VALUE self) {
  fio_buf_info_s message = FIO_BUF_INFO0;
  fio_buf_info_s recipient_pk = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(message, 0, NULL, 1),
                  IODINE_ARG_BUF(recipient_pk, 0, "recipient_pk", 1));

  if (recipient_pk.len != 32)
    rb_raise(rb_eArgError,
             "recipient_pk must be 32 bytes (got %zu)",
             recipient_pk.len);

  /* Output is message + 48 bytes overhead (32 ephemeral pk + 16 mac) */
  size_t out_len = message.len + 48;
  VALUE ciphertext = rb_str_buf_new(out_len);
  rb_str_set_len(ciphertext, out_len);

  int result =
      fio_x25519_encrypt((uint8_t *)RSTRING_PTR(ciphertext),
                         message.buf,
                         message.len,
                         (fio_crypto_enc_fn *)fio_chacha20_poly1305_enc,
                         (const uint8_t *)recipient_pk.buf);

  if (result != 0)
    rb_raise(rb_eRuntimeError, "Encryption failed");

  return ciphertext;
  (void)self;
}

/**
 * Decrypts a message using X25519 public-key encryption (ECIES).
 *
 * @param ciphertext [String] Ciphertext from X25519.encrypt
 * @param secret_key: [String] 32-byte recipient's secret key
 * @return [String] Decrypted plaintext
 * @raise [RuntimeError] if decryption fails (authentication error)
 */
FIO_SFUNC VALUE iodine_crypto_x25519_decrypt(int argc,
                                             VALUE *argv,
                                             VALUE self) {
  fio_buf_info_s ciphertext = FIO_BUF_INFO0;
  fio_buf_info_s sk = FIO_BUF_INFO0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(ciphertext, 0, NULL, 1),
                  IODINE_ARG_BUF(sk, 0, "secret_key", 1));

  if (sk.len != 32)
    rb_raise(rb_eArgError, "secret_key must be 32 bytes (got %zu)", sk.len);
  if (ciphertext.len < 48)
    rb_raise(rb_eArgError,
             "ciphertext too short (minimum 48 bytes, got %zu)",
             ciphertext.len);

  size_t out_len = ciphertext.len - 48;
  VALUE plaintext = rb_str_buf_new(out_len);
  rb_str_set_len(plaintext, out_len);

  int result =
      fio_x25519_decrypt((uint8_t *)RSTRING_PTR(plaintext),
                         (const uint8_t *)ciphertext.buf,
                         ciphertext.len,
                         (fio_crypto_dec_fn *)fio_chacha20_poly1305_dec,
                         (const uint8_t *)sk.buf);

  if (result != 0)
    rb_raise(rb_eRuntimeError, "Decryption failed (authentication error)");

  return plaintext;
  (void)self;
}

/* *****************************************************************************
HKDF Key Derivation (RFC 5869)
***************************************************************************** */

/**
 * Derives keying material using HKDF (RFC 5869).
 *
 * @param ikm: [String] Input keying material
 * @param salt: [String, nil] Optional salt (random value)
 * @param info: [String, nil] Optional context/application info
 * @param length: [Integer] Desired output length (default: 32)
 * @param sha384: [Boolean] Use SHA-384 instead of SHA-256 (default: false)
 * @return [String] Derived key material
 */
FIO_SFUNC VALUE iodine_crypto_hkdf_derive(int argc, VALUE *argv, VALUE self) {
  fio_buf_info_s ikm = FIO_BUF_INFO0;
  fio_buf_info_s salt = FIO_BUF_INFO0;
  fio_buf_info_s info = FIO_BUF_INFO0;
  int64_t length = 32;
  uint8_t sha384 = 0;

  iodine_rb2c_arg(argc,
                  argv,
                  IODINE_ARG_BUF(ikm, 0, "ikm", 1),
                  IODINE_ARG_BUF(salt, 0, "salt", 0),
                  IODINE_ARG_BUF(info, 0, "info", 0),
                  IODINE_ARG_NUM(length, 0, "length", 0),
                  IODINE_ARG_BOOL(sha384, 0, "sha384", 0));

  /* Max output: 255 * hash_len (32 for SHA-256, 48 for SHA-384) */
  size_t max_len = sha384 ? (size_t)(48 * 255) : (size_t)(32 * 255);
  if (length < 1 || (size_t)length > max_len)
    rb_raise(rb_eArgError,
             "length must be between 1 and %zu (got %" PRId64 ")",
             max_len,
             length);

  VALUE okm = rb_str_buf_new(length);
  rb_str_set_len(okm, length);

  fio_hkdf(RSTRING_PTR(okm),
           (size_t)length,
           salt.buf,
           salt.len,
           ikm.buf,
           ikm.len,
           info.buf,
           info.len,
           (int)sha384);

  return okm;
  (void)self;
}

/* *****************************************************************************
Module Initialization
***************************************************************************** */

static void Init_Iodine_Crypto(void) {
  /* Iodine::Base::Crypto */
  iodine_rb_CRYPTO = rb_define_module_under(iodine_rb_IODINE_BASE, "Crypto");
  STORE.hold(iodine_rb_CRYPTO);

  /* Iodine::Base::Crypto::ChaCha20Poly1305 */
  iodine_rb_CHACHA20POLY1305 =
      rb_define_module_under(iodine_rb_CRYPTO, "ChaCha20Poly1305");
  STORE.hold(iodine_rb_CHACHA20POLY1305);
  rb_define_module_function(iodine_rb_CHACHA20POLY1305,
                            "encrypt",
                            iodine_crypto_chacha_encrypt,
                            -1);
  rb_define_module_function(iodine_rb_CHACHA20POLY1305,
                            "decrypt",
                            iodine_crypto_chacha_decrypt,
                            -1);

  /* Iodine::Base::Crypto::Ed25519 */
  iodine_rb_ED25519 = rb_define_module_under(iodine_rb_CRYPTO, "Ed25519");
  STORE.hold(iodine_rb_ED25519);
  rb_define_module_function(iodine_rb_ED25519,
                            "keypair",
                            iodine_crypto_ed25519_keypair,
                            0);
  rb_define_module_function(iodine_rb_ED25519,
                            "public_key",
                            iodine_crypto_ed25519_public_key,
                            -1);
  rb_define_module_function(iodine_rb_ED25519,
                            "sign",
                            iodine_crypto_ed25519_sign,
                            -1);
  rb_define_module_function(iodine_rb_ED25519,
                            "verify",
                            iodine_crypto_ed25519_verify,
                            -1);

  /* Iodine::Base::Crypto::X25519 */
  iodine_rb_X25519 = rb_define_module_under(iodine_rb_CRYPTO, "X25519");
  STORE.hold(iodine_rb_X25519);
  rb_define_module_function(iodine_rb_X25519,
                            "keypair",
                            iodine_crypto_x25519_keypair,
                            0);
  rb_define_module_function(iodine_rb_X25519,
                            "public_key",
                            iodine_crypto_x25519_public_key,
                            -1);
  rb_define_module_function(iodine_rb_X25519,
                            "shared_secret",
                            iodine_crypto_x25519_shared_secret,
                            -1);
  rb_define_module_function(iodine_rb_X25519,
                            "encrypt",
                            iodine_crypto_x25519_encrypt,
                            -1);
  rb_define_module_function(iodine_rb_X25519,
                            "decrypt",
                            iodine_crypto_x25519_decrypt,
                            -1);

  /* Iodine::Base::Crypto::HKDF */
  iodine_rb_HKDF = rb_define_module_under(iodine_rb_CRYPTO, "HKDF");
  STORE.hold(iodine_rb_HKDF);
  rb_define_module_function(iodine_rb_HKDF,
                            "derive",
                            iodine_crypto_hkdf_derive,
                            -1);
}

#endif /* H___IODINE_CRYPTO___H */
