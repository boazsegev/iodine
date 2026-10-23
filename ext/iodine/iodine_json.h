#ifndef H___IODINE_JSON___H
#define H___IODINE_JSON___H
#include "iodine.h"

/* *****************************************************************************
Iodine JSON - Fast JSON Parsing and Stringification

This module provides the Iodine::JSON Ruby module for JSON operations.
It's primarily used internally for WebSocket message serialization but
is also exposed as a public API.

Features:
- Fast JSON stringification (Ruby objects to JSON strings)
- JSON parsing (JSON strings to Ruby objects)
- Beautified/pretty-printed JSON output
- Handles nested arrays, hashes, strings, numbers, booleans, nil

Performance Notes:
- Stringification is fast (single memory copy from C to Ruby)
- Parsing is slower than alternatives (double copy: JSON->FIOBJ->Ruby)
- For production JSON work, consider using the 'oj' gem instead

Ruby API (Iodine::JSON):
- Iodine::JSON.parse(json_string)     - Parse JSON to Ruby objects
- Iodine::JSON.stringify(object)      - Convert Ruby object to JSON
- Iodine::JSON.dump(object)           - Alias for stringify
- Iodine::JSON.beautify(object)       - Pretty-printed JSON output
- Iodine::JSON.beautify_slow(object)  - Alternative beautifier via FIOBJ
- Iodine::JSON.parse_slow(json)       - Alternative parser via FIOBJ
***************************************************************************** */

/* *****************************************************************************
JSON Stringifier - Ruby to JSON String Conversion
***************************************************************************** */

/** Minimal-overhead fio_bstr writer for the JSON stringifier.
 *
 * Mirrors the bstr's len/capa, so the hot path is an inline capacity check
 * followed by a raw write - avoiding the per-call overhead of the generic
 * fio_bstr_write API (which validates capacity on every single write).
 * Growth goes through fio_bstr_reserve with added slack (capa/4) for
 * geometric, amortized O(1) appends.
 *
 * NOTE: all writes MUST go through these helpers - len/capa are mirrors.
 */
typedef struct {
  char *s;     /* the fio_bstr (may be NULL) */
  size_t len;  /* mirrors fio_bstr_len(s) */
  size_t capa; /* mirrors fio_bstr_info(s).capa (0 when s is NULL) */
} iodine_json_w_s;

/** Ensures room for `add` more bytes (plus the NUL terminator).
 *  Returns non-zero on success, zero on allocation failure (write skipped,
 *  truncating the output - same semantics as fio_bstr_write). */
FIO_IFUNC int iodine_json_w_need(iodine_json_w_s *w, size_t add) {
  if (FIO_LIKELY(w->capa - w->len > add))
    return 1;
  /* sync the bstr metadata (our len is a mirror) before reserve reads it */
  w->s = fio_bstr_len_set(w->s, w->len);
  /* grow with geometric slack (capa/4) - amortized O(1) appends */
  char *n = fio_bstr_reserve(w->s, add + (w->capa >> 2) + 1);
  fio_str_info_s i = fio_bstr_info(n);
  if (FIO_UNLIKELY(i.capa - i.len <= add))
    return 0; /* allocation failed */
  w->s = n;
  w->capa = i.capa; /* i.len == w->len (mirrors) */
  return 1;
}

FIO_IFUNC void iodine_json_w_write(iodine_json_w_s *w,
                                   const void *src,
                                   size_t len) {
  if (FIO_LIKELY(iodine_json_w_need(w, len))) {
    FIO_MEMCPY(w->s + w->len, src, len);
    w->len += len;
  }
}

FIO_IFUNC void iodine_json_w_ch(iodine_json_w_s *w, const char c) {
  if (FIO_LIKELY(iodine_json_w_need(w, 1)))
    w->s[w->len++] = c;
}

FIO_IFUNC void iodine_json_w_i(iodine_json_w_s *w, int64_t num) {
  /* write the number directly into the buffer (max 20 digits + sign + NUL) */
  if (FIO_LIKELY(iodine_json_w_need(w, 24)))
    w->len += fio_ltoa(w->s + w->len, num, 10);
}

/** Writes `num` to `dest` (up to 39 bytes + NUL), matching the output of
 *  fio_ftoa(dest, num, 10) - i.e., snprintf("%g") with the ".0" fixup -
 *  without the snprintf overhead.
 *
 *  Falls back to fio_ftoa for non-finite values, extreme exponents, and
 *  values landing on a rounding boundary (where the exact snprintf
 *  half-to-even rounding of the exact decimal expansion is required).
 *
 *  NOTE: uses the C STL's internal fio___aton_dec2d for correctly-rounded
 *  powers of ten. If a future fio-stl.h sync renames it, this breaks loudly
 *  at compile time (safe). */
static size_t iodine_json_ftoa(char *dest, double num) {
  if (FIO_UNLIKELY(!isfinite(num)))
    return fio_ftoa(dest, num, 10); /* "NaN" / "Infinity" / "-Infinity" */
  char *p = dest;
  double a = num;
  uint64_t bits;
  memcpy(&bits, &num, 8);
  if (bits >> 63) {
    *p++ = '-';
    a = -a;
  }
  if (a == 0) { /* %g prints "0" (or "-0"), fio_ftoa adds ".0" */
    memcpy(p, "0.0", 4);
    return (size_t)(p + 3 - dest);
  }
  /* decimal exponent estimate: a = m * 2^e2, m in [0.5, 1) */
  int e2;
  frexp(a, &e2);
  int e10 = (int)(((int64_t)e2 * 78913) >> 18); /* ~floor(log10(a)) +/- 1 */
  if (FIO_UNLIKELY(e10 < -302 || e10 > 302))
    return fio_ftoa(dest, num, 10); /* pow10 out of table range */
  /* scale to a 6 digit mantissa (q ~= a * 10^(5-e10) in [1e5, 1e6)) */
  double q = a * fio___aton_dec2d(1, 5 - e10);
  if (q >= 1e6 || q < 1e5) { /* correct the estimate (off by one at most) */
    e10 += (q >= 1e6) ? 1 : -1;
    q = a * fio___aton_dec2d(1, 5 - e10);
  }
  double fl = floor(q);
  if (FIO_UNLIKELY(q - fl > 0.4999999 && q - fl < 0.5000001))
    return fio_ftoa(dest, num, 10); /* rounding boundary - exact tie break */
  uint64_t r = (uint64_t)fl + (q - fl >= 0.5);
  if (FIO_UNLIKELY(r == 1000000)) { /* rounded up to the next power of 10 */
    r = 100000;
    ++e10;
  }
  /* the 6 digits of r (d.ddddd * 10^e10) */
  char dig[6];
  dig[5] = (char)('0' + (r % 10));
  r /= 10;
  dig[4] = (char)('0' + (r % 10));
  r /= 10;
  dig[3] = (char)('0' + (r % 10));
  r /= 10;
  dig[2] = (char)('0' + (r % 10));
  r /= 10;
  dig[1] = (char)('0' + (r % 10));
  r /= 10;
  dig[0] = (char)('0' + r);
  if (e10 >= -4 && e10 < 6) {
    /* %f style (trimming trailing zeros after the decimal point) */
    if (e10 >= 0) {
      const int idig = e10 + 1; /* integer digits (1..6) */
      memcpy(p, dig, (size_t)idig);
      p += idig;
      int f = 5;
      while (f >= idig && dig[f] == '0')
        --f;
      if (f >= idig) {
        *p++ = '.';
        memcpy(p, dig + idig, (size_t)(f - idig + 1));
        p += f - idig + 1;
      } else {
        memcpy(p, ".0", 3); /* fio_ftoa's ".0" fixup */
        p += 2;
      }
    } else {
      memcpy(p, "0.", 2);
      p += 2;
      for (int z = -e10 - 1; z--;)
        *p++ = '0';
      int f = 5;
      while (f > 0 && dig[f] == '0')
        --f;
      memcpy(p, dig, (size_t)(f + 1));
      p += f + 1;
    }
  } else {
    /* %e style: d[.ddddd]e+XX (trimming trailing zeros in the mantissa) */
    *p++ = dig[0];
    int f = 5;
    while (f > 0 && dig[f] == '0')
      --f;
    if (f) {
      *p++ = '.';
      memcpy(p, dig + 1, (size_t)f);
      p += f;
    }
    *p++ = 'e';
    *p++ = (e10 < 0) ? '-' : '+';
    int x = (e10 < 0) ? -e10 : e10;
    if (x >= 100)
      *p++ = (char)('0' + (x / 100));
    *p++ = (char)('0' + ((x / 10) % 10));
    *p++ = (char)('0' + (x % 10));
  }
  *p = 0;
  return (size_t)(p - dest);
}

FIO_IFUNC void iodine_json_w_f(iodine_json_w_s *w, double num) {
  /* write the float directly into the buffer (max 39 bytes + NUL) */
  if (FIO_LIKELY(iodine_json_w_need(w, 40)))
    w->len += iodine_json_ftoa(w->s + w->len, num);
}

/** SWAR escape detection: returns non-zero if any byte in the word needs
 *  escaping (< 0x20, '"', '\\', 0x7F) or UTF-8 validation (>= 0x80).
 *  Byte-order independent (mask is only tested against zero). */
FIO_IFUNC uint64_t iodine_json_swar_dirty(uint64_t v) {
  const uint64_t hi = 0x8080808080808080ULL;
  uint64_t dirty = (v - 0x2020202020202020ULL) & ~v & hi; /* bytes < 0x20 */
  uint64_t x = v ^ 0x2222222222222222ULL;                 /* '"' */
  dirty |= (x - 0x0101010101010101ULL) & ~x & hi;
  x = v ^ 0x5C5C5C5C5C5C5C5CULL; /* '\\' */
  dirty |= (x - 0x0101010101010101ULL) & ~x & hi;
  x = v ^ 0x7F7F7F7F7F7F7F7FULL; /* 0x7F */
  dirty |= (x - 0x0101010101010101ULL) & ~x & hi;
  return dirty | (v & hi); /* bytes >= 0x80 */
}

/** Writes a quoted JSON string, escaping as needed (single pass).
 *  Escape semantics identical to fio_bstr_write_escape: simple escapes for
 *  \b \f \n \r \t \\ \", \u00XX for control characters (incl. 0x7F),
 *  valid UTF-8 sequences copied as-is, invalid bytes escaped as \xXX. */
static void iodine_json_w_string(iodine_json_w_s *w,
                                 const char *src,
                                 size_t len) {
  const uint8_t *s = (const uint8_t *)src;
  const uint8_t *e = s + len;
  const uint8_t *run = s;
  const uint8_t *p = s;
  iodine_json_w_ch(w, '"');
  while (p < e) {
    /* SWAR fast scan: skip 16 clean bytes at a time */
    while ((size_t)(e - p) >= 16) {
      uint64_t v0, v1;
      memcpy(&v0, p, 8);
      memcpy(&v1, p + 8, 8);
      if (iodine_json_swar_dirty(v0) | iodine_json_swar_dirty(v1))
        break;
      p += 16;
    }
    /* byte-scan the dirty block (or the tail) */
    const uint8_t *block_end = ((size_t)(e - p) >= 16) ? p + 16 : e;
    while (p < block_end) {
      const uint8_t c = *p;
      /* fast path: printable ASCII (0x20..0x7E), except '"' and '\\' */
      if (FIO_LIKELY((uint8_t)(c - 0x20) < (uint8_t)0x5F && c != '"' &&
                     c != '\\')) {
        ++p;
        continue;
      }
      const size_t utf8_len = fio_utf8_char_len(p);
      if (utf8_len > 1 && (size_t)(e - p) >= utf8_len) {
        p += utf8_len; /* valid UTF-8 sequence - copied as-is with the run */
        continue;
      }
      /* (a truncated sequence at the end of the input is escaped, byte-wise) */
      /* flush the clean run and escape this byte */
      iodine_json_w_write(w, run, (size_t)(p - run));
      ++p;
      run = p;
      switch (c) {
      case '\b': iodine_json_w_write(w, "\\b", 2); break;
      case '\f': iodine_json_w_write(w, "\\f", 2); break;
      case '\n': iodine_json_w_write(w, "\\n", 2); break;
      case '\r': iodine_json_w_write(w, "\\r", 2); break;
      case '\t': iodine_json_w_write(w, "\\t", 2); break;
      case '\\': iodine_json_w_write(w, "\\\\", 2); break;
      case '"': iodine_json_w_write(w, "\\\"", 2); break;
      default: {
        /* control characters: \u00XX - invalid UTF-8 bytes: \xXX */
        char buf[6] = {'\\',
                       'u',
                       '0',
                       '0',
                       (char)fio_i2c((unsigned char)(c >> 4)),
                       (char)fio_i2c((unsigned char)(c & 15))};
        if (c & 128) {
          buf[1] = 'x';
          buf[2] = buf[4];
          buf[3] = buf[5];
          iodine_json_w_write(w, buf, 4);
        } else {
          iodine_json_w_write(w, buf, 6);
        }
      } break;
      }
    }
  }
  iodine_json_w_write(w, run, (size_t)(e - run));
  iodine_json_w_ch(w, '"');
}

static void iodine_json_w_value(iodine_json_w_s *w, VALUE o);

static void iodine_json_w_key(iodine_json_w_s *w, VALUE tmp) {
  /* Note: keys MUST be Strings  */
  switch (rb_type(tmp)) {
  case RUBY_T_BIGNUM: /* Bignum (Windows: Integers >= 2^30) */
    tmp = rb_big2str(tmp, 10);
    goto string_key;
  case RUBY_T_SYMBOL: tmp = rb_sym2str(tmp); /* fall through */
  case RUBY_T_STRING:
  string_key:
    iodine_json_w_string(w, RSTRING_PTR(tmp), (size_t)RSTRING_LEN(tmp));
    break;
  case RUBY_T_FIXNUM:
    iodine_json_w_ch(w, '"');
    iodine_json_w_i(w, NUM2LL(tmp));
    iodine_json_w_ch(w, '"');
    break;
  case RUBY_T_TRUE: iodine_json_w_write(w, "\"true\"", 6); break;
  case RUBY_T_FALSE: iodine_json_w_write(w, "\"false\"", 7); break;
  case RUBY_T_NIL: iodine_json_w_write(w, "\"null\"", 6); break;
  default:
    tmp = rb_any_to_s(tmp);
    if (RB_TYPE_P(tmp, RUBY_T_STRING))
      goto string_key;
    iodine_json_w_write(w, "\"error\"", 7);
  }
}

static int iodine_json_hash_each_callback(VALUE k, VALUE v, VALUE w_) {
  iodine_json_w_s *w = (iodine_json_w_s *)w_;
  iodine_json_w_key(w, k);
  iodine_json_w_ch(w, ':');
  iodine_json_w_value(w, v);
  iodine_json_w_ch(w, ',');
  return ST_CONTINUE;
}

/* Converts a VALUE to JSON, appending to the writer's bstr. */
static void iodine_json_w_value(iodine_json_w_s *w, VALUE o) {
  size_t tmp;
  switch (rb_type(o)) {
  case RUBY_T_NIL: iodine_json_w_write(w, "null", 4); return;
  case RUBY_T_TRUE: iodine_json_w_write(w, "true", 4); return;
  case RUBY_T_FALSE: iodine_json_w_write(w, "false", 5); return;
  case RUBY_T_ARRAY:
    tmp = rb_array_len(o);
    if (!tmp) {
      iodine_json_w_write(w, "[]", 2);
      return;
    }
    iodine_json_w_ch(w, '[');
    for (size_t i = 0; i < tmp; ++i) {
      iodine_json_w_value(w, RARRAY_PTR(o)[i]);
      iodine_json_w_ch(w, ',');
    }
    if (FIO_LIKELY(w->len && w->s[w->len - 1] == ','))
      w->s[w->len - 1] = ']';
    else
      iodine_json_w_ch(w, ']');
    return;
  case RUBY_T_HASH:
    tmp = rb_hash_size_num(o);
    if (!tmp) {
      iodine_json_w_write(w, "{}", 2);
      return;
    }
    iodine_json_w_ch(w, '{');
    rb_hash_foreach(o, iodine_json_hash_each_callback, (VALUE)w);
    if (FIO_LIKELY(w->len && w->s[w->len - 1] == ','))
      w->s[w->len - 1] = '}';
    else
      iodine_json_w_ch(w, '}');
    return;
  case RUBY_T_FIXNUM: iodine_json_w_i(w, RB_NUM2LL(o)); return;
  case RUBY_T_BIGNUM: /* Bignum (Windows: Integers >= 2^30) */
    o = rb_big2str(o, 10);
    iodine_json_w_write(w, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    return;
  case RUBY_T_FLOAT: iodine_json_w_f(w, RFLOAT_VALUE(o)); return;
  case RUBY_T_SYMBOL: o = rb_sym2str(o); /* fall through */
  case RUBY_T_STRING:
  add_string:
    iodine_json_w_string(w, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    return;
  default:
    if (1) {
      VALUE tmp = rb_check_funcall(o, IODINE_TO_JSON_ID, 0, NULL);
      if (tmp != RUBY_Qundef && RB_TYPE_P(tmp, RUBY_T_STRING)) {
        iodine_json_w_write(w, RSTRING_PTR(tmp), (size_t)RSTRING_LEN(tmp));
        return;
      }
      o = rb_funcallv(o, IODINE_TO_S_ID, 0, NULL);
    }
    if (!RB_TYPE_P(o, RUBY_T_STRING)) {
      FIO_LOG_ERROR("Iodine::JSON.stringify called with an object that doesn't "
                    "respond to #to_s.");
      iodine_json_w_write(w, "null", 4);
      return;
    }
    goto add_string;
  }
}

/* Converts a VALUE to a fio_bstr */
static char *iodine_json_stringify2bstr(char *dest, VALUE o) {
  iodine_json_w_s w = {.s = dest, .len = 0, .capa = 0};
  if (dest) {
    fio_str_info_s i = fio_bstr_info(dest);
    w.len = i.len;
    w.capa = i.capa;
  }
  iodine_json_w_value(&w, o);
  return fio_bstr_len_set(w.s, w.len);
}

/* *****************************************************************************
JSON Beautifier - Pretty-Printed JSON Output
***************************************************************************** */

typedef struct {
  iodine_json_w_s w;
  size_t t;
} iodine_json_beautify2bstr_s;

static void iodine_json_beautify2bstr(iodine_json_beautify2bstr_s *d, VALUE o);

FIO_IFUNC void iodine_json_beautify2bstr_pad(iodine_json_beautify2bstr_s *d) {
  if (!d->t)
    return;
  iodine_json_w_ch(&d->w, '\n');
  if (iodine_json_w_need(&d->w, d->t)) {
    FIO_MEMSET(d->w.s + d->w.len, '\t', d->t);
    d->w.len += d->t;
  }
}

static int iodine_json_hash_each_beautify_callback(VALUE k,
                                                   VALUE v,
                                                   VALUE dest_) {
  iodine_json_beautify2bstr_s *d = (iodine_json_beautify2bstr_s *)dest_;
  iodine_json_beautify2bstr_pad(d);
  iodine_json_w_key(&d->w, k);
  iodine_json_w_write(&d->w, ": ", 2);
  iodine_json_beautify2bstr(d, v);
  iodine_json_w_ch(&d->w, ',');
  return ST_CONTINUE;
}

/* Converts a VALUE to a fio_bstr */
static void iodine_json_beautify2bstr(iodine_json_beautify2bstr_s *d, VALUE o) {
  iodine_json_w_s *w = &d->w;
  size_t tmp;
  switch (rb_type(o)) {
  case RUBY_T_NIL: iodine_json_w_write(w, "null", 4); return;
  case RUBY_T_TRUE: iodine_json_w_write(w, "true", 4); return;
  case RUBY_T_FALSE: iodine_json_w_write(w, "false", 5); return;
  case RUBY_T_ARRAY:
    tmp = rb_array_len(o);
    if (!tmp) {
      iodine_json_w_write(w, "[]", 2);
      return;
    }
    iodine_json_w_ch(w, '[');
    ++d->t;
    for (size_t i = 0; i < tmp; ++i) {
      iodine_json_beautify2bstr_pad(d);
      iodine_json_beautify2bstr(d, RARRAY_PTR(o)[i]);
      iodine_json_w_ch(w, ',');
    }
    if (FIO_LIKELY(w->len && w->s[w->len - 1] == ','))
      w->s[w->len - 1] = '\n';
    --d->t;
    iodine_json_beautify2bstr_pad(d);
    iodine_json_w_ch(w, ']');
    return;
  case RUBY_T_HASH:
    tmp = rb_hash_size_num(o);
    if (!tmp) {
      iodine_json_w_write(w, "{}", 2);
      return;
    }
    iodine_json_w_ch(w, '{');
    ++d->t;
    rb_hash_foreach(o, iodine_json_hash_each_beautify_callback, (VALUE)d);
    if (FIO_LIKELY(w->len && w->s[w->len - 1] == ','))
      w->s[w->len - 1] = '\n';
    --d->t;
    iodine_json_beautify2bstr_pad(d);
    iodine_json_w_ch(w, '}');
    return;
  case RUBY_T_FIXNUM: iodine_json_w_i(w, RB_NUM2LL(o)); return;
  case RUBY_T_BIGNUM: /* Bignum (Windows: Integers >= 2^30) */
    o = rb_big2str(o, 10);
    iodine_json_w_write(w, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    return;
  case RUBY_T_FLOAT: iodine_json_w_f(w, RFLOAT_VALUE(o)); return;
  case RUBY_T_SYMBOL: o = rb_sym2str(o); /* fall through */
  case RUBY_T_STRING:
  add_string:
    iodine_json_w_string(w, RSTRING_PTR(o), (size_t)RSTRING_LEN(o));
    return;
  default:
    if (1) {
      VALUE tmp = rb_check_funcall(o, IODINE_TO_JSON_ID, 0, NULL);
      if (tmp != RUBY_Qundef && RB_TYPE_P(tmp, RUBY_T_STRING)) {
        iodine_json_w_write(w, RSTRING_PTR(tmp), (size_t)RSTRING_LEN(tmp));
        return;
      }
      o = rb_funcallv(o, IODINE_TO_S_ID, 0, NULL);
      if (RB_TYPE_P(o, RUBY_T_STRING))
        goto add_string;
    }
    FIO_LOG_ERROR("Iodine::JSON.stringify called with an object that doesn't "
                  "respond to neither #to_json nor #to_s.");
    iodine_json_w_write(w, "null", 4);
    return;
  }
}

/* *****************************************************************************
FIOBJ => Ruby Bridge - Convert facil.io Objects to Ruby
***************************************************************************** */

typedef struct iodine_fiobj2ruby_task_s {
  VALUE out;
} iodine_fiobj2ruby_task_s;

static VALUE iodine_fiobj2ruby(FIOBJ o);

static int iodine_fiobj2ruby_array_task(fiobj_array_each_s *e) {
  rb_ary_push((VALUE)e->udata, iodine_fiobj2ruby(e->value));
  return 0;
}
static int iodine_fiobj2ruby_hash_task(fiobj_hash_each_s *e) {
  VALUE k = iodine_fiobj2ruby(e->key);
  VALUE v = iodine_fiobj2ruby(e->value);
  rb_hash_aset((VALUE)e->udata, k, v);
  return 0;
}

/**
 * Converts a FIOBJ (facil.io object) to a Ruby VALUE.
 *
 * Recursively converts FIOBJ types to their Ruby equivalents:
 * - FIOBJ_T_TRUE/FALSE -> Qtrue/Qfalse
 * - FIOBJ_T_NUMBER -> Fixnum/Bignum
 * - FIOBJ_T_FLOAT -> Float
 * - FIOBJ_T_STRING -> String
 * - FIOBJ_T_ARRAY -> Array
 * - FIOBJ_T_HASH -> Hash
 * - FIOBJ_T_NULL/INVALID -> Qnil
 *
 * @param o The FIOBJ to convert
 * @return Ruby VALUE equivalent
 *
 * @note Does NOT place VALUE in STORE automatically.
 */
static VALUE iodine_fiobj2ruby(FIOBJ o) {
  VALUE r;
  switch (FIOBJ_TYPE(o)) {
  case FIOBJ_T_TRUE: return Qtrue;
  case FIOBJ_T_FALSE: return Qfalse;
  case FIOBJ_T_NUMBER: return RB_LL2NUM(fiobj_num2i(o));
  case FIOBJ_T_FLOAT: return rb_float_new(fiobj_float2f(o));
  case FIOBJ_T_STRING: return rb_str_new(fiobj_str_ptr(o), fiobj_str_len(o));
  case FIOBJ_T_ARRAY:
    STORE.gc_stop();
    r = rb_ary_new_capa(fiobj_array_count(o));
    fiobj_array_each(o, iodine_fiobj2ruby_array_task, (void *)r, 0);
    STORE.gc_start();
    return r;
  case FIOBJ_T_HASH:
    STORE.gc_stop();
    r = rb_hash_new();
    fiobj_hash_each(o, iodine_fiobj2ruby_hash_task, (void *)r, 0);
    STORE.gc_start();
    return r;
  // case FIOBJ_T_NULL: /* fall through */
  // case FIOBJ_T_INVALID: /* fall through */
  default: return Qnil;
  }
}

/* *****************************************************************************
Ruby => FIOBJ Bridge - Convert Ruby Objects to facil.io Objects
***************************************************************************** */

typedef struct iodine_ruby2fiobj_task_s {
  FIOBJ out;
} iodine_ruby2fiobj_task_s;

static FIOBJ iodine_ruby2fiobj(VALUE o);

static int iodine_ruby2fiobj_hash_each_task(VALUE n, VALUE v, VALUE h_) {
  FIOBJ h = (FIOBJ)h_;
  FIOBJ key = iodine_ruby2fiobj(n);
  fiobj_hash_set(h, key, iodine_ruby2fiobj(v), NULL);
  fiobj_free(key); /* by default, keys aren't owned by the hash */
  return ST_CONTINUE;
}

/**
 * Converts a Ruby VALUE to a FIOBJ (facil.io object).
 *
 * Recursively converts Ruby types to their FIOBJ equivalents:
 * - Qtrue/Qfalse -> fiobj_true()/fiobj_false()
 * - Fixnum -> fiobj_num_new()
 * - Float -> fiobj_float_new()
 * - Symbol/String -> fiobj_str_new_cstr()
 * - Array -> fiobj_array_new()
 * - Hash -> fiobj_hash_new()
 * - nil/undef -> fiobj_null()
 * - Other -> calls #to_json or #to_s
 *
 * @param o The Ruby VALUE to convert
 * @return FIOBJ equivalent
 *
 * @note Does NOT place VALUE in STORE automatically.
 */
static FIOBJ iodine_ruby2fiobj(VALUE o) {
  FIOBJ r;
  VALUE tmp;
  switch (rb_type(o)) {
  case RUBY_T_TRUE: return fiobj_true();
  case RUBY_T_FALSE: return fiobj_false();
  case RUBY_T_FIXNUM: return fiobj_num_new((intptr_t)RB_NUM2LL(o));
  case RUBY_T_BIGNUM: /* Bignum (Windows: Integers >= 2^30) */
    if (1) {
      size_t c = 0;
      o = rb_big2str(o, 10);
      return fiobj_json_parse((fio_str_info_s)IODINE_RSTR_INFO(o), &c);
    }
  case RUBY_T_FLOAT: return fiobj_float_new(rb_float_value(o));
  case RUBY_T_SYMBOL: o = rb_sym_to_s(o); /* fall through */
  case RUBY_T_STRING: return fiobj_str_new_cstr(RSTRING_PTR(o), RSTRING_LEN(o));
  case RUBY_T_ARRAY:
    if (1) {
      const size_t alen = rb_array_len(o);
      r = fiobj_array_new();
      if (alen)
        fiobj_array_reserve(r, (int64_t)alen);
      for (size_t i = 0; i < alen; ++i)
        fiobj_array_push(r, iodine_ruby2fiobj(RARRAY_PTR(o)[i]));
      return r;
    }
  case RUBY_T_HASH:
    r = fiobj_hash_new();
    if (rb_hash_size_num(o))
      fiobj_hash_reserve(r, rb_hash_size_num(o));
    rb_hash_foreach(o, iodine_ruby2fiobj_hash_each_task, (VALUE)r);
    return r;
  case RUBY_T_NIL:   /* fall through */
  case RUBY_T_UNDEF: /* fall through */
  case RUBY_T_NONE: return fiobj_null();
  default:
    tmp = rb_check_funcall(o, IODINE_TO_JSON_ID, 0, NULL);
    if (tmp != RUBY_Qundef && RB_TYPE_P(tmp, RUBY_T_STRING)) {
      size_t c = 0;
      r = fiobj_json_parse((fio_str_info_s)IODINE_RSTR_INFO(tmp), &c);
      return r;
    }
    o = rb_any_to_s(o);
    if (RB_TYPE_P(o, RUBY_T_STRING))
      return fiobj_str_new_cstr(RSTRING_PTR(o), RSTRING_LEN(o));
    return fiobj_null();
  }
}

/* *****************************************************************************
JSON Parser (Indirect) - Parse via FIOBJ intermediate
***************************************************************************** */

/**
 * Parses a JSON string to Ruby objects via FIOBJ intermediate.
 *
 * This is the slower parsing path that:
 * 1. Parses JSON string to FIOBJ tree
 * 2. Converts FIOBJ tree to Ruby objects
 *
 * @param self The Iodine::JSON module
 * @param rstr The JSON string to parse (String)
 * @return Ruby object tree (Hash, Array, String, etc.)
 *
 * Ruby: Iodine::JSON.parse_slow(json_string)
 */
static VALUE iodine_json_parse_indirect(VALUE self, VALUE rstr) {
  VALUE r = Qnil;
  rb_check_type(rstr, RUBY_T_STRING);
  size_t consumed = 0;
  FIOBJ tmp =
      fiobj_json_parse((fio_str_info_s)IODINE_RSTR_INFO(rstr), &consumed);
  STORE.gc_stop();
  r = iodine_fiobj2ruby(tmp);
  STORE.gc_start();
  fiobj_free(tmp);
  return r;
}

/* *****************************************************************************
JSON Parser (Direct) - Parse Directly to Ruby Objects

This parser uses facil.io's streaming JSON parser with callbacks that
create Ruby objects directly, avoiding the FIOBJ intermediate step.
This is the faster parsing path used by Iodine::JSON.parse().
***************************************************************************** */

/** JSON parser callback implementations for direct Ruby object creation */
/** NULL object was detected. Returns new object as `void *`. */
FIO_SFUNC fio_json_obj_result_s iodine___json_on_null(void *udata) {
  (void)udata;
  return (fio_json_obj_result_s){.obj = (void *)Qnil};
}
/** TRUE object was detected. Returns new object as `void *`. */
FIO_SFUNC fio_json_obj_result_s iodine___json_on_true(void *udata) {
  (void)udata;
  return (fio_json_obj_result_s){.obj = (void *)Qtrue};
}
/** FALSE object was detected. Returns new object as `void *`. */
FIO_SFUNC fio_json_obj_result_s iodine___json_on_false(void *udata) {
  (void)udata;
  return (fio_json_obj_result_s){.obj = (void *)Qfalse};
}
/** Number was detected (long long). Returns new object as `void *`. */
FIO_SFUNC fio_json_obj_result_s iodine___json_on_number(void *udata,
                                                        int64_t i) {
  (void)udata;
  return (fio_json_obj_result_s){.obj = (void *)LL2NUM(((long long)i))};
}
/** Float was detected (double).Returns new object as `void *`.  */
FIO_SFUNC fio_json_obj_result_s iodine___json_on_float(void *udata, double f) {
  (void)udata;
  return (fio_json_obj_result_s){.obj = (void *)DBL2NUM(f)};
}
/** String was detected (int / float). update `pos` to point at ending */
FIO_SFUNC fio_json_obj_result_s iodine___json_on_string(void *udata,
                                                        fio_buf_info_s input) {
  (void)udata;
  const void *start = input.buf;
  size_t len = input.len;
  VALUE str;
  if (len < 4096) {
    FIO_STR_INFO_TMP_VAR(buf, 4096);
    fio_string_write_unescape(&buf, NULL, start, len);
    str = rb_str_new(buf.buf, buf.len);
  } else {
    char *tmp = fio_bstr_write_unescape(NULL, start, len);
    str = rb_str_new(tmp, fio_bstr_len(tmp));
    fio_bstr_free(tmp);
  }
  return (fio_json_obj_result_s){.obj = (void *)str};
}
FIO_SFUNC fio_json_obj_result_s
iodine___json_on_string_simple(void *udata, fio_buf_info_s input) {
  (void)udata;
  VALUE str = rb_str_new(input.buf, input.len);
  return (fio_json_obj_result_s){.obj = (void *)str};
}
/** Dictionary opens at `key` in `ctx` (NULL ctx = owned root). Places the
 * map in its parent, reusing an existing Hash at `key` when present
 * (duplicate map keys merge, matching the FIOBJ parser). JSON parsing keeps
 * last-wins semantics: replaced values are NOT reported through `old` -
 * conflict resolution is reserved for the HTTP params layer. */
FIO_SFUNC fio_json_push_result_s iodine___json_on_map(void *udata,
                                                      void *ctx,
                                                      fio_buf_info_s key) {
  (void)udata;
  VALUE map;
  if (!ctx)
    return (fio_json_push_result_s){.obj = (void *)rb_hash_new()};
  if (!key.buf) { /* array parent: append */
    map = rb_hash_new();
    rb_ary_push((VALUE)ctx, map);
    return (fio_json_push_result_s){.obj = (void *)map};
  }
  VALUE k = rb_str_new(key.buf, (long)key.len);
  VALUE cur = rb_hash_lookup2((VALUE)ctx, k, Qundef);
  if (cur != Qundef && RB_TYPE_P(cur, RUBY_T_HASH))
    return (fio_json_push_result_s){
        .obj = (void *)cur}; /* reuse matching map (merge) */
  map = rb_hash_new();
  rb_hash_aset((VALUE)ctx, k, map);
  return (fio_json_push_result_s){.obj = (void *)map};
}
/** Array opens at `key` in `ctx` (NULL ctx = owned root). Same placement
 * and reuse contract as iodine___json_on_map (last-wins, `old` unused). */
FIO_SFUNC fio_json_push_result_s iodine___json_on_array(void *udata,
                                                        void *ctx,
                                                        fio_buf_info_s key) {
  (void)udata;
  VALUE ary;
  if (!ctx)
    return (fio_json_push_result_s){.obj = (void *)rb_ary_new()};
  if (!key.buf) { /* array parent: append */
    ary = rb_ary_new();
    rb_ary_push((VALUE)ctx, ary);
    return (fio_json_push_result_s){.obj = (void *)ary};
  }
  VALUE k = rb_str_new(key.buf, (long)key.len);
  VALUE cur = rb_hash_lookup2((VALUE)ctx, k, Qundef);
  if (cur != Qundef && RB_TYPE_P(cur, RUBY_T_ARRAY))
    return (fio_json_push_result_s){.obj =
                                        (void *)cur}; /* reuse matching array */
  ary = rb_ary_new();
  rb_hash_aset((VALUE)ctx, k, ary);
  return (fio_json_push_result_s){.obj = (void *)ary};
}
/** Map entry detected (`key` is borrowed). Last value wins, matching the
 * FIOBJ parser. Returns no replaced object to the parser. */
FIO_SFUNC fio_json_obj_result_s iodine___json_map_push(void *udata,
                                                       void *map,
                                                       fio_buf_info_s key,
                                                       void *val) {
  (void)udata;
  rb_hash_aset((VALUE)map, rb_str_new(key.buf, (long)key.len), (VALUE)val);
  return (fio_json_obj_result_s){0};
}
/** Array entry detected. Returns non-zero on error. */
FIO_SFUNC int iodine___json_array_push(void *udata, void *ctx, void *val) {
  (void)udata;
  rb_ary_push((VALUE)ctx, (VALUE)val);
  return 0;
}
/** Conflict-resolution type checks (values are Ruby objects). */
FIO_SFUNC int iodine___json_is_array(void *udata, void *ctx) {
  (void)udata;
  return RB_TYPE_P((VALUE)ctx, RUBY_T_ARRAY);
}
FIO_SFUNC int iodine___json_is_map(void *udata, void *ctx) {
  (void)udata;
  return RB_TYPE_P((VALUE)ctx, RUBY_T_HASH);
}
/** Ruby owns values; no manual cleanup is required on parser errors. */
FIO_SFUNC void iodine___json_free_unused_object(void *udata, void *ctx) {
  (void)udata, (void)ctx; /* Ruby objects are GC managed */
}
static fio_json_parser_callbacks_s IODINE_JSON_PARSER_CALLBACKS = {
    .on_null = iodine___json_on_null,
    .on_true = iodine___json_on_true,
    .on_false = iodine___json_on_false,
    .on_number = iodine___json_on_number,
    .on_float = iodine___json_on_float,
    .on_string = iodine___json_on_string,
    .on_string_simple = iodine___json_on_string_simple,
    .on_map = iodine___json_on_map,
    .on_array = iodine___json_on_array,
    .map_push = iodine___json_map_push,
    .array_push = iodine___json_array_push,
    .is_array = iodine___json_is_array,
    .is_map = iodine___json_is_map,
    .free_unused_object = iodine___json_free_unused_object,
};

/* *****************************************************************************
API - Public Ruby Methods
***************************************************************************** */

/**
 * Parses a JSON string to Ruby objects (fast path).
 *
 * Uses the direct parser that creates Ruby objects without
 * an intermediate FIOBJ representation.
 *
 * @param self The Iodine::JSON module
 * @param rstr The JSON string to parse (String)
 * @return Ruby object tree (Hash, Array, String, etc.)
 *
 * Ruby: Iodine::JSON.parse(json_string)
 */
static VALUE iodine_json_parse(VALUE self, VALUE rstr) {
  VALUE r = Qnil;
  rb_check_type(rstr, RUBY_T_STRING);
  STORE.gc_stop();
  fio_json_result_s result = fio_json_parse(&IODINE_JSON_PARSER_CALLBACKS,
                                            NULL,
                                            NULL,
                                            RSTRING_PTR(rstr),
                                            RSTRING_LEN(rstr));
  STORE.gc_start();
  r = result.err ? Qnil : (VALUE)result.ctx;
  STORE.cache(r);
  rb_str_buf_new(1); /* force GC to run if Ruby needs it */
  return r;
}

/**
 * Converts a Ruby object to a JSON string.
 *
 * Handles all standard Ruby types (Hash, Array, String, Numeric,
 * true, false, nil). For other objects, calls #to_json or #to_s.
 *
 * @param self The Iodine::JSON module
 * @param object The Ruby object to stringify
 * @return JSON string representation
 *
 * Ruby: Iodine::JSON.stringify(object)
 *       Iodine::JSON.dump(object)  # alias
 */
static VALUE iodine_json_stringify(VALUE self, VALUE object) {
  VALUE r = Qnil;
  char *str = fio_bstr_reserve(NULL, ((size_t)1 << 12) - 64);
  str = iodine_json_stringify2bstr(str, object);
  r = rb_str_new(str, (long)fio_bstr_len(str));
  fio_bstr_free(str);
  return r;
}

/**
 * Converts a Ruby object to a pretty-printed JSON string (slow path).
 *
 * Uses FIOBJ intermediate for formatting. Slower but produces
 * nicely indented output.
 *
 * @param self The Iodine::JSON module
 * @param object The Ruby object to stringify
 * @return Pretty-printed JSON string
 *
 * Ruby: Iodine::JSON.beautify_slow(object)
 */
static VALUE iodine_json_pretty(VALUE self, VALUE object) {
  VALUE r = Qnil;
  FIOBJ o = iodine_ruby2fiobj(object);
  FIOBJ out = fiobj2json(FIOBJ_T_INVALID, o, 1);
  fiobj_free(o);
  r = rb_str_new(fiobj_str_ptr(out), (long)fiobj_str_len(out));
  fiobj_free(out);
  return r;
}

/**
 * Converts a Ruby object to a pretty-printed JSON string (fast path).
 *
 * Uses direct stringification with indentation. Faster than
 * beautify_slow but produces similar output.
 *
 * @param self The Iodine::JSON module
 * @param object The Ruby object to stringify
 * @return Pretty-printed JSON string with newlines and tabs
 *
 * Ruby: Iodine::JSON.beautify(object)
 */
static VALUE iodine_json_beautify(VALUE self, VALUE object) {
  VALUE r = Qnil;
  iodine_json_beautify2bstr_s dest = {.w = {.s = NULL, .len = 0, .capa = 0},
                                      .t = 0};
  dest.w.s = fio_bstr_reserve(NULL, ((size_t)1 << 12) - 64);
  fio_str_info_s i = fio_bstr_info(dest.w.s);
  dest.w.len = i.len;
  dest.w.capa = i.capa;
  iodine_json_beautify2bstr(&dest, object);
  dest.w.s = fio_bstr_len_set(dest.w.s, dest.w.len);
  r = rb_str_new(dest.w.s, (long)fio_bstr_len(dest.w.s));
  fio_bstr_free(dest.w.s);
  return r;
}

/** Initialize Iodine::JSON */ // clang-format off
/**
Iodine::JSON exposes the {Iodine::Connection#write} fallback behavior when called with non-String objects.

The fallback behavior is similar to (though faster than) calling:

        client.write(Iodine::JSON.stringify(data))

If you want to work with JSON, consider using the `oj` gem.

This API is mostly to test for Iodine JSON input/output errors and reflects what the C layer sees.

## Performance

Performance... could be better.

Converting Ruby objects into a JSON String (Strinfigying) should be fast even though the String data is copied twice, once to C and then to Ruby.

However, Converting a JSON object into Ruby Objects is currently slow and it is better to use the `oj` gem or even the Ruby builtin parser.

The reason is simple - the implementation is designed to create C objects (C Hash Maps, C Arrays, etc'), not Ruby objects. When converting from a String to Ruby Objevts, the data is copied twice, once to C and then to Ruby.

This especially effects parsing, where more objects are allocated,
whereas {Iodine::JSON.stringify} only (re)copies the String data which is a single continuous block of memory.

That's why {Iodine::JSON.stringify} is significantly faster than the Ruby `object.to_json` approach,
yet slower than `JSON.parse(json_string)`.

          require 'oj' rescue nil
          require 'benchmark/ips'
          require 'json'
          require 'iodine'

          def benchmark_json
            # make a big data store with nothings
            data_1000 = []
            1000.times do
              tmp = {f: rand() };
              tmp[:i] = (tmp[:f] * 1000000).to_i
              tmp[:str] = tmp[:i].to_s
              tmp[:sym] = tmp[:str].to_sym
              tmp[:ary] = []
              tmp[:ary_empty] = []
              tmp[:hash_empty] = Hash.new
              100.times {|i| tmp[:ary] << i }
              data_1000 << tmp
            end
            3.times do
              json_string = data_1000.to_json
              puts "-----"
              puts "Benchmark #{data_1000.length} item tree, and #{json_string.length} bytes of JSON"
              # benchmark stringification
              Benchmark.ips do |x|
                x.report("      Ruby obj.to_json") do |times|
                  data_1000.to_json
                end
                x.report("Iodine::JSON.stringify") do |times|
                  Iodine::JSON.stringify(data_1000)
                end
                if(defined?(Oj))
                  x.report("               Oj.dump") do |times|
                    Oj.dump(data_1000)
                  end
                end
                x.compare!
              end ; nil
              # benchmark parsing
              Benchmark.ips do |x|
                x.report("   Ruby JSON.parse") do |times|
                  JSON.parse(json_string)
                end
                x.report("Iodine::JSON.parse") do |times|
                  Iodine::JSON.parse(json_string)
                end
                if(defined?(Oj))
                  x.report("           Oj.load") do |times|
                    Oj.load(json_string)
                  end
                end
                x.compare!
              end
              data_1000 = data_1000.slice(0, (data_1000.length / 10))
              nil
            end
          end

          benchmark_json

*/
static void Init_Iodine_JSON(void) {
  VALUE m = rb_define_module_under(iodine_rb_IODINE, "JSON"); // clang-format on
  rb_define_singleton_method(m, "parse", iodine_json_parse, 1);
  rb_define_singleton_method(m, "parse_slow", iodine_json_parse_indirect, 1);
  rb_define_singleton_method(m, "stringify", iodine_json_stringify, 1);
  rb_define_singleton_method(m, "beautify_slow", iodine_json_pretty, 1);
  rb_define_singleton_method(m, "beautify", iodine_json_beautify, 1);
  rb_define_singleton_method(m, "dump", iodine_json_stringify, 1);
}
#endif /* H___IODINE_JSON___H */
