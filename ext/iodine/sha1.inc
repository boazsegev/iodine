/* This code is public-domain - it is based on libcrypt
 * placed in the public domain by Wei Dai and other contributors.
 */
// gcc -Wall -DSHA1TEST -o sha1test sha1.c && ./sha1test

#include <stdint.h>
#include <string.h>

#ifdef __BIG_ENDIAN__
#define SHA_BIG_ENDIAN
#elif defined __LITTLE_ENDIAN__
/* override */
#elif defined __BYTE_ORDER
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SHA_BIG_ENDIAN
#endif
#else                // ! defined __LITTLE_ENDIAN__
#include <endian.h>  // machine/endian.h
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SHA_BIG_ENDIAN
#endif
#endif

/* header */

#define HASH_LENGTH 20
#define BLOCK_LENGTH 64

typedef struct sha1nfo {
  uint32_t buffer[BLOCK_LENGTH / 4];
  uint32_t state[HASH_LENGTH / 4];
  uint32_t byteCount;
  uint8_t bufferOffset;
  uint8_t keyBuffer[BLOCK_LENGTH];
  uint8_t innerHash[HASH_LENGTH];
} sha1nfo;

/* public API - prototypes - TODO: doxygen*/

/**
 */
static void sha1_init(sha1nfo* s);
/**
 */
static void sha1_writebyte(sha1nfo* s, uint8_t data);
/**
 */
static void sha1_write(sha1nfo* s, const char* data, size_t len);
/**
 */
static uint8_t* sha1_result(sha1nfo* s);
/**
 */
// static void sha1_initHmac(sha1nfo* s, const uint8_t* key, int keyLength);
// /**
//  */
// static uint8_t* sha1_resultHmac(sha1nfo* s);

/* code */
#define SHA1_K0 0x5a827999
#define SHA1_K20 0x6ed9eba1
#define SHA1_K40 0x8f1bbcdc
#define SHA1_K60 0xca62c1d6

static void sha1_init(sha1nfo* s) {
  s->state[0] = 0x67452301;
  s->state[1] = 0xefcdab89;
  s->state[2] = 0x98badcfe;
  s->state[3] = 0x10325476;
  s->state[4] = 0xc3d2e1f0;
  s->byteCount = 0;
  s->bufferOffset = 0;
}

static uint32_t sha1_rol32(uint32_t number, uint8_t bits) {
  return ((number << bits) | (number >> (32 - bits)));
}

static void sha1_hashBlock(sha1nfo* s) {
  uint8_t i;
  uint32_t a, b, c, d, e, t;

  a = s->state[0];
  b = s->state[1];
  c = s->state[2];
  d = s->state[3];
  e = s->state[4];
  for (i = 0; i < 80; i++) {
    if (i >= 16) {
      t = s->buffer[(i + 13) & 15] ^ s->buffer[(i + 8) & 15] ^
          s->buffer[(i + 2) & 15] ^ s->buffer[i & 15];
      s->buffer[i & 15] = sha1_rol32(t, 1);
    }
    if (i < 20) {
      t = (d ^ (b & (c ^ d))) + SHA1_K0;
    } else if (i < 40) {
      t = (b ^ c ^ d) + SHA1_K20;
    } else if (i < 60) {
      t = ((b & c) | (d & (b | c))) + SHA1_K40;
    } else {
      t = (b ^ c ^ d) + SHA1_K60;
    }
    t += sha1_rol32(a, 5) + e + s->buffer[i & 15];
    e = d;
    d = c;
    c = sha1_rol32(b, 30);
    b = a;
    a = t;
  }
  s->state[0] += a;
  s->state[1] += b;
  s->state[2] += c;
  s->state[3] += d;
  s->state[4] += e;
}

static void sha1_addUncounted(sha1nfo* s, uint8_t data) {
  uint8_t* const b = (uint8_t*)s->buffer;
#ifdef SHA_BIG_ENDIAN
  b[s->bufferOffset] = data;
#else
  b[s->bufferOffset ^ 3] = data;
#endif
  s->bufferOffset++;
  if (s->bufferOffset == BLOCK_LENGTH) {
    sha1_hashBlock(s);
    s->bufferOffset = 0;
  }
}

static void sha1_writebyte(sha1nfo* s, uint8_t data) {
  ++s->byteCount;
  sha1_addUncounted(s, data);
}

static void sha1_write(sha1nfo* s, const char* data, size_t len) {
  for (; len--;)
    sha1_writebyte(s, (uint8_t)*data++);
}

static void sha1_pad(sha1nfo* s) {
  // Implement SHA-1 padding (fips180-2 Â§5.1.1)

  // Pad with 0x80 followed by 0x00 until the end of the block
  sha1_addUncounted(s, 0x80);
  while (s->bufferOffset != 56)
    sha1_addUncounted(s, 0x00);

  // Append length in the last 8 bytes
  sha1_addUncounted(s, 0);  // We're only using 32 bit lengths
  sha1_addUncounted(s, 0);  // But SHA-1 supports 64 bit lengths
  sha1_addUncounted(s, 0);  // So zero pad the top bits
  sha1_addUncounted(s, s->byteCount >> 29);  // Shifting to multiply by 8
  sha1_addUncounted(
      s, s->byteCount >> 21);  // as SHA-1 supports bitstreams as well as
  sha1_addUncounted(s, s->byteCount >> 13);  // byte.
  sha1_addUncounted(s, s->byteCount >> 5);
  sha1_addUncounted(s, s->byteCount << 3);
}

static uint8_t* sha1_result(sha1nfo* s) {
  // Pad to complete the last block
  sha1_pad(s);

#ifndef SHA_BIG_ENDIAN
  // Swap byte order back
  int i;
  for (i = 0; i < 5; i++) {
    s->state[i] = (((s->state[i]) << 24) & 0xff000000) |
                  (((s->state[i]) << 8) & 0x00ff0000) |
                  (((s->state[i]) >> 8) & 0x0000ff00) |
                  (((s->state[i]) >> 24) & 0x000000ff);
  }
#endif

  // Return pointer to hash (20 characters)
  return (uint8_t*)s->state;
}

// #define HMAC_IPAD 0x36
// #define HMAC_OPAD 0x5c
//
// static void sha1_initHmac(sha1nfo* s, const uint8_t* key, int keyLength) {
//   uint8_t i;
//   memset(s->keyBuffer, 0, BLOCK_LENGTH);
//   if (keyLength > BLOCK_LENGTH) {
//     // Hash long keys
//     sha1_init(s);
//     for (; keyLength--;)
//       sha1_writebyte(s, *key++);
//     memcpy(s->keyBuffer, sha1_result(s), HASH_LENGTH);
//   } else {
//     // Block length keys are used as is
//     memcpy(s->keyBuffer, key, keyLength);
//   }
//   // Start inner hash
//   sha1_init(s);
//   for (i = 0; i < BLOCK_LENGTH; i++) {
//     sha1_writebyte(s, s->keyBuffer[i] ^ HMAC_IPAD);
//   }
// }
//
// static uint8_t* sha1_resultHmac(sha1nfo* s) {
//   uint8_t i;
//   // Complete inner hash
//   memcpy(s->innerHash, sha1_result(s), HASH_LENGTH);
//   // Calculate outer hash
//   sha1_init(s);
//   for (i = 0; i < BLOCK_LENGTH; i++)
//     sha1_writebyte(s, s->keyBuffer[i] ^ HMAC_OPAD);
//   for (i = 0; i < HASH_LENGTH; i++)
//     sha1_writebyte(s, s->innerHash[i]);
//   return sha1_result(s);
// }
// #define IS_BIG_ENDIAN (!*(unsigned char*)&(uint16_t){1})

// I wrote this one (Boaz Segev)... anyone is free to copy this one.
static int ws_base64_encode(char* data, char* target, int len) {
  int written = 0;
  struct {
    unsigned b2 : 2;
    unsigned b6 : 6;
  } * _2b6;
  struct {
    unsigned b6 : 6;
    unsigned b2 : 2;
  } * _6b2;
  struct {
    unsigned b1 : 4;
    unsigned b2 : 4;
  } * _4b4;
  static char codes[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
  while (len >= 3) {
    _2b6 = (void*)data;
    target[0] = codes[_2b6->b6];
    _4b4 = (void*)data + 1;
    target[1] = codes[(_2b6->b2 << 4) | _4b4->b2];
    _6b2 = (void*)data + 2;
    target[2] = codes[_4b4->b1 << 2 | _6b2->b2];
    target[3] = codes[_6b2->b6];

    target += 4;
    data += 3;
    len -= 3;
    written += 4;
  }
  if (len == 2) {
    _2b6 = (void*)data;
    target[0] = codes[_2b6->b6];
    _4b4 = (void*)data + 1;
    target[1] = codes[(_2b6->b2 << 4) | _4b4->b2];
    _6b2 = (void*)data + 2;
    target[2] = codes[_4b4->b1 << 2 | _6b2->b2];
    target[3] = '=';
  } else if (len == 1) {
    _2b6 = (void*)data;
    target[0] = codes[_2b6->b6];
    target[1] = codes[(_2b6->b2 << 4)];
    target[2] = '=';
    target[3] = '=';
  }
  target += 4;
  written += 4;
  return written;
}
