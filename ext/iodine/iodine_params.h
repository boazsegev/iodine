#ifndef H___IODINE_PARAMS___H
#define H___IODINE_PARAMS___H
/* *****************************************************************************

                        Iodine::Connection — NeoRack `params`

Implements the NeoRack `params` extension (v0.0.1) on top of the C STL HTTP
params helpers (`fio_http_query_parse` / `fio_http_body_parse` in fio-stl.h):

    e.params        # => Hash (query first, then body; memoized per request)
    e.query2params  # => merges the query string (once)
    e.body2params   # => merges a supported body (once)

The same mutable, request-scoped Hash backs all three methods; it lives in the
connection's internal store (never in the app-facing `e[...]` KV map).

Design notes:

- Query fields parse directly into the request Hash. Body sources parse into a
  staged Hash and merge only after the C parser returns successfully. This
  prevents incomplete JSON members and aborted multipart files from becoming
  visible, and lets Ruby exceptions propagate after parser-owned C resources
  have been released.
- Conflict resolution is performed by this adapter using Ruby values. JSON
  false uses Qundef only while crossing the STL's void-pointer callback API;
  it is decoded before storage. No Ruby object is ever used as a NULL/absence
  sentinel in the merge logic.
- Parser callbacks protect every allocating Ruby operation. An exception is
  rooted in the connection, parsing stops through normal C error paths, and
  the original exception is re-raised after the parser returns. rb_ensure
  restores body position and temporary roots. GC remains enabled.
- Multipart file parts are `Iodine::Base::Upload` objects (an internal class):
  a binary stream (`read` / `rewind` / `size` / `save` / `close` / `closed?`)
  with metadata readers and Rack-style `[]` / `[]=` access. Uploads are scalar
  "other" values for conflict resolution, not Hashes.
- Upload data lives in an `fio_data_s` store (RAM, spilling to a temporary
  file past FIO_DATA_RAM_LIMIT). A non-owning intrusive list indexes open
  uploads for deterministic request-end cleanup. The list never marks Ruby
  objects; params / staging roots and the active-upload slot provide GC roots.
  Manual close, request cleanup and Upload finalization share one idempotent
  close/unlink path. Data operations share a native mutex with no-GVL saves.
- The Rack compatibility map is embedded directly in Upload. Required keys
  read metadata directly unless a view-only override exists; MiniMap buckets
  are allocated only for String / Symbol writes. Other key types use a lazy
  Hash fallback so compatibility access keeps ordinary Hash semantics.

Field budget:

- Query pairs, form fields and multipart parts share one request budget. Each
  parse is allowed one extra field; a reported count above the remaining
  budget proves overflow without confusing malformed input or empty segments
  with excess fields. JSON is not counted (STL behavior).

Parser errors and remaining contract deviation:

- Depth and generic query / URL-encoded parser failures raise RangeError
  after field accounting; body-read failures raise IOError. JSON and multipart
  ERROR combine malformed / generic failures and discard the staged body.
  Invalid percent escapes remain literal. Direct JSON preserves the parser's
  integer error but handles every parse error as staged discard, as permitted.
- The query / form name-depth bound is FIO_JSON_MAX_DEPTH (128), not the
  extension's suggested default of 32.

***************************************************************************** */

/* *****************************************************************************
Upload (NeoRack `params` extension Upload interface)
***************************************************************************** */

typedef struct iodine_upload_s {
  fio_data_s data; /* RAM store; spills to a temporary file past the limit */
  VALUE filename;  /* String (client basename, path components stripped) */
  VALUE type;      /* String or Qnil */
  VALUE name;      /* String (form field name) */
  VALUE headers;   /* String (raw part headers) or Qnil */
  VALUE rack_fallback;        /* Lazy Hash for non-String / non-Symbol keys */
  iodine_minimap_s rack_view; /* Embedded; buckets allocated only when used */
  fio_thread_mutex_t lock;    /* Serializes data operations and no-GVL save */
  struct iodine_upload_s *next;
  struct iodine_upload_s **prev; /* Address of the link pointing to this node */
  uint8_t closed;
} iodine_upload_s;

FIO_SFUNC void iodine___upload_close(iodine_upload_s *s);

static void iodine_upload_mark(void *ptr_) {
  iodine_upload_s *s = (iodine_upload_s *)ptr_;
  rb_gc_mark(s->filename);
  rb_gc_mark(s->type);
  rb_gc_mark(s->name);
  rb_gc_mark(s->headers);
  rb_gc_mark(s->rack_fallback);
  iodine_minimap_gc_mark(&s->rack_view);
}

static void iodine_upload_free(void *ptr_) {
  iodine_upload_s *s = (iodine_upload_s *)ptr_;
  iodine___upload_close(s);
  iodine_hmap_destroy(&s->rack_view.map);
  fio_thread_mutex_destroy(&s->lock);
  ruby_xfree(s);
}

static size_t iodine_upload_data_size(const void *ptr_) {
  iodine_upload_s *s = (iodine_upload_s *)ptr_;
  return sizeof(*s) + (iodine_hmap_capa(&s->rack_view.map) *
                       sizeof(s->rack_view.map.map[0]));
}

static const rb_data_type_t IODINE_UPLOAD_DATA_TYPE = {
    .wrap_struct_name = "IodineBaseUpload",
    .function =
        {
            .dmark = iodine_upload_mark,
            .dfree = iodine_upload_free,
            .dsize = iodine_upload_data_size,
        },
    .data = NULL,
    .flags = RUBY_TYPED_FREE_IMMEDIATELY,
};

static VALUE iodine_rb_BASE_UPLOAD; /* Iodine::Base::Upload */
static VALUE IODINE_PARAMS_EMPTY_KEY;

static ID IODINE_PARAMS_FILENAME_ID; /* :filename */
static ID IODINE_PARAMS_TYPE_ID;     /* :type */
static ID IODINE_PARAMS_NAME_ID;     /* :name */
static ID IODINE_PARAMS_TEMPFILE_ID; /* :tempfile */
static ID IODINE_PARAMS_HEAD_ID;     /* :head */

static VALUE iodine_upload_alloc(VALUE klass) {
  iodine_upload_s *s = NULL;
  VALUE self = TypedData_Make_Struct(klass,
                                     iodine_upload_s,
                                     &IODINE_UPLOAD_DATA_TYPE,
                                     s);
  *s = (iodine_upload_s){0};
  s->data = (fio_data_s)FIO_DATA_INIT;
  s->lock = (fio_thread_mutex_t)FIO_THREAD_MUTEX_INIT;
  s->filename = Qnil;
  s->type = Qnil;
  s->name = Qnil;
  s->headers = Qnil;
  s->rack_fallback = Qnil;
  return self;
}

static iodine_upload_s *iodine_upload_ptr(VALUE self) {
  iodine_upload_s *s;
  TypedData_Get_Struct(self, iodine_upload_s, &IODINE_UPLOAD_DATA_TYPE, s);
  return s;
}

FIO_SFUNC void iodine___upload_unlink(iodine_upload_s *s) {
  if (!s->prev)
    return;
  *s->prev = s->next;
  if (s->next)
    s->next->prev = s->prev;
  s->prev = NULL;
  s->next = NULL;
}

/* Called with the GVL. The list is a cleanup index, never a GC root. */
FIO_SFUNC void iodine___upload_close(iodine_upload_s *s) {
  fio_thread_mutex_lock(&s->lock);
  iodine___upload_unlink(s);
  if (!s->closed) {
    s->closed = 1;
    fio_data_destroy(&s->data);
  }
  fio_thread_mutex_unlock(&s->lock);
}

FIO_SFUNC void iodine___upload_track(iodine_connection_s *c,
                                     iodine_upload_s *s) {
  s->next = c->open_uploads;
  if (s->next)
    s->next->prev = &s->next;
  s->prev = &c->open_uploads;
  c->open_uploads = s;
  fio_atomic_or(&c->flags, IODINE_CONNECTION_PARAMS_HAS_UPLOAD);
}

FIO_SFUNC VALUE iodine___upload_unlock(VALUE s_) {
  iodine_upload_s *s = (iodine_upload_s *)s_;
  fio_thread_mutex_unlock(&s->lock);
  return Qnil;
}

typedef struct {
  iodine_upload_s *s;
  VALUE out;
  size_t limit;
  uint8_t length_given;
} iodine___upload_read_s;

/* Runs with s->lock held; rb_ensure releases it on every Ruby exception. */
FIO_SFUNC VALUE iodine___upload_read_locked(VALUE args_) {
  iodine___upload_read_s *args = (iodine___upload_read_s *)args_;
  iodine_upload_s *s = args->s;
  if (s->closed)
    rb_raise(rb_eIOError, "can't read from a closed upload");
  const size_t position = fio_data_pos(&s->data);
  const size_t length = fio_data_len(&s->data);
  const size_t remaining = position < length ? length - position : 0;
  const size_t wanted = args->limit < remaining ? args->limit : remaining;
  if (wanted > (size_t)LONG_MAX)
    rb_raise(rb_eRangeError, "read length exceeds the Ruby String limit");
  if (args->out != Qnil) {
    rb_str_resize(args->out, 0); /* validate / clear before consuming bytes */
    if (!args->length_given)
      rb_enc_associate(args->out, IodineBinaryEncoding);
  }
  if (args->length_given && args->limit && position >= length)
    return Qnil;
  fio_str_info_s r = fio_data_read(&s->data, wanted);
  if (args->limit && !r.len && fio_data_pos(&s->data) < fio_data_len(&s->data))
    rb_raise(rb_eIOError, "upload read error (store reported a short read)");
  if (args->out != Qnil) {
    if (r.len)
      rb_str_cat(args->out, r.buf, (long)r.len);
    return args->out; /* Explicit lengths preserve out_string's encoding. */
  }
  return rb_enc_str_new(r.buf, (long)r.len, IodineBinaryEncoding);
}

/**
 * Ruby: read(length = nil, out_string = nil) -> String or nil.
 * Follows binary IO#read EOF and out_string semantics; IOError after close.
 */
static VALUE iodine_upload_read(int argc, VALUE *argv, VALUE self) {
  iodine___upload_read_s args = {.s = iodine_upload_ptr(self),
                                 .out = Qnil,
                                 .limit = (size_t)-1,
                                 .length_given = 0};
  VALUE len_v = Qnil;
  rb_scan_args(argc, argv, "02", &len_v, &args.out);
  if (len_v != Qnil) {
    if (!RB_INTEGER_TYPE_P(len_v))
      rb_raise(rb_eTypeError, "length must be an Integer or nil");
    long long l = NUM2LL(len_v);
    if (l < 0)
      rb_raise(rb_eArgError, "length must not be negative");
    args.limit = (unsigned long long)l > (unsigned long long)SIZE_MAX
                     ? SIZE_MAX
                     : (size_t)l;
    args.length_given = 1;
  }
  if (args.out != Qnil)
    Check_Type(args.out, T_STRING);
  fio_thread_mutex_lock(&args.s->lock);
  return rb_ensure(iodine___upload_read_locked,
                   (VALUE)&args,
                   iodine___upload_unlock,
                   (VALUE)args.s);
}

/** Ruby: rewind -> 0 (IOError after close). */
static VALUE iodine_upload_rewind(VALUE self) {
  iodine_upload_s *s = iodine_upload_ptr(self);
  fio_thread_mutex_lock(&s->lock);
  if (s->closed) {
    fio_thread_mutex_unlock(&s->lock);
    rb_raise(rb_eIOError, "can't rewind a closed upload");
  }
  fio_data_seek(&s->data, 0);
  fio_thread_mutex_unlock(&s->lock);
  return INT2FIX(0);
}

/** Ruby: size -> Integer (never moves the read position; IOError after close).
 */
static VALUE iodine_upload_size(VALUE self) {
  iodine_upload_s *s = iodine_upload_ptr(self);
  fio_thread_mutex_lock(&s->lock);
  if (s->closed) {
    fio_thread_mutex_unlock(&s->lock);
    rb_raise(rb_eIOError, "closed upload");
  }
  size_t len = fio_data_len(&s->data);
  fio_thread_mutex_unlock(&s->lock);
  return ULL2NUM((unsigned long long)len);
}

/** fio_data_save argument carrier for the no-GVL call (large uploads). */
typedef struct {
  iodine_upload_s *s;
  const char *filename;
  size_t size;
  int result;
  int error;
  uint8_t closed;
} iodine___upload_save_s;

/* Lock, save and unlock in one no-GVL callback. close can never wait for this
   thread to reacquire the GVL while still holding the upload lock. */
static void *iodine___upload_save_no_gvl(void *p_) {
  iodine___upload_save_s *p = (iodine___upload_save_s *)p_;
  fio_thread_mutex_lock(&p->s->lock);
  if (p->s->closed) {
    p->closed = 1;
  } else {
    p->result = fio_data_save(&p->s->data, p->filename);
    p->size = fio_data_len(&p->s->data);
    p->error = p->result == -1 ? errno : 0;
  }
  fio_thread_mutex_unlock(&p->s->lock);
  return NULL;
}

typedef struct {
  iodine___upload_save_s args;
  VALUE path;
  uint8_t path_locked;
} iodine___upload_save_guard_s;

FIO_SFUNC VALUE iodine___upload_save_large(VALUE guard_) {
  iodine___upload_save_guard_s *g = (iodine___upload_save_guard_s *)guard_;
  if (!OBJ_FROZEN(g->path)) { /* rb_str_locktmp rejects frozen Strings */
    rb_str_locktmp(g->path);
    g->path_locked = 1;
  }
  g->args.filename = RSTRING_PTR(g->path);
  rb_thread_call_without_gvl(iodine___upload_save_no_gvl, &g->args, NULL, NULL);
  return Qnil;
}

FIO_SFUNC VALUE iodine___upload_save_large_ensure(VALUE guard_) {
  iodine___upload_save_guard_s *g = (iodine___upload_save_guard_s *)guard_;
  if (g->path_locked)
    rb_str_unlocktmp(g->path);
  STORE.release(g->path);
  return Qnil;
}

/**
 * Ruby: save(path) -> Integer (bytes written; IOError after close).
 *
 * fio_data_save writes atomically through a sibling file and preserves the
 * upload's read position. Payloads of 256KiB or more save outside the GVL.
 * The path String is rooted; mutable paths are locked during that window.
 */
static VALUE iodine_upload_save(VALUE self, VALUE path) {
  iodine_upload_s *s = iodine_upload_ptr(self);
  fio_thread_mutex_lock(&s->lock);
  if (s->closed) {
    fio_thread_mutex_unlock(&s->lock);
    rb_raise(rb_eIOError, "can't save a closed upload");
  }
  size_t len = fio_data_len(&s->data);
  fio_thread_mutex_unlock(&s->lock);

  if (!RB_TYPE_P(path, RUBY_T_STRING))
    rb_raise(rb_eTypeError, "`path` should be a String");
  if (memchr(RSTRING_PTR(path), 0, (size_t)RSTRING_LEN(path)))
    rb_raise(rb_eArgError, "path contains a NUL byte");

  iodine___upload_save_s args = {
      .s = s,
      .filename = RSTRING_PTR(path),
      .size = 0,
      .result = -1,
      .error = 0,
      .closed = 0,
  };
  if (len >= (1ULL << 18)) {
    iodine___upload_save_guard_s guard = {.args = args,
                                          .path = path,
                                          .path_locked = 0};
    STORE.hold(path);
    rb_ensure(iodine___upload_save_large,
              (VALUE)&guard,
              iodine___upload_save_large_ensure,
              (VALUE)&guard);
    args = guard.args;
  } else {
    fio_thread_mutex_lock(&s->lock);
    if (s->closed) {
      fio_thread_mutex_unlock(&s->lock);
      rb_raise(rb_eIOError, "can't save a closed upload");
    }
    args.result = fio_data_save(&s->data, args.filename);
    args.size = fio_data_len(&s->data);
    args.error = args.result == -1 ? errno : 0;
    fio_thread_mutex_unlock(&s->lock);
  }
  if (args.closed)
    rb_raise(rb_eIOError, "can't save a closed upload");
  if (args.result == -1) {
    errno = args.error;
    rb_sys_fail("upload save failed");
  }
  return ULL2NUM((unsigned long long)args.size);
}

/** Ruby: close -> nil (idempotent; releases temporary storage). */
static VALUE iodine_upload_close(VALUE self) {
  iodine___upload_close(iodine_upload_ptr(self));
  return Qnil;
}

/** Ruby: closed? -> true / false. */
static VALUE iodine_upload_closed_p(VALUE self) {
  return iodine_upload_ptr(self)->closed ? Qtrue : Qfalse;
}

/* Metadata readers (String or nil, as provided by the client). */
static VALUE iodine_upload_filename(VALUE self) {
  return iodine_upload_ptr(self)->filename;
}
static VALUE iodine_upload_type(VALUE self) {
  return iodine_upload_ptr(self)->type;
}
static VALUE iodine_upload_name(VALUE self) {
  return iodine_upload_ptr(self)->name;
}
static VALUE iodine_upload_headers(VALUE self) {
  return iodine_upload_ptr(self)->headers;
}

/** Returns a required Rack value, or Qundef if the key uses the fallback. */
FIO_SFUNC VALUE iodine___upload_required_value(VALUE self,
                                               iodine_upload_s *s,
                                               VALUE key) {
  if (!RB_SYMBOL_P(key))
    return Qundef;
  ID id = rb_sym2id(key);
  if (id == IODINE_PARAMS_FILENAME_ID)
    return s->filename;
  if (id == IODINE_PARAMS_TYPE_ID)
    return s->type;
  if (id == IODINE_PARAMS_NAME_ID)
    return s->name;
  if (id == IODINE_PARAMS_TEMPFILE_ID)
    return self;
  if (id == IODINE_PARAMS_HEAD_ID)
    return s->headers;
  return Qundef;
}

FIO_SFUNC int iodine___upload_minimap_key_p(VALUE key) {
  return RB_SYMBOL_P(key) || RB_TYPE_P(key, RUBY_T_STRING);
}

/** Ruby: upload[key] -> Rack-style access (`:tempfile` returns the upload). */
static VALUE iodine_upload_aref(VALUE self, VALUE key) {
  iodine_upload_s *s = iodine_upload_ptr(self);
  if (!iodine___upload_minimap_key_p(key))
    return s->rack_fallback == Qnil ? Qnil
                                    : rb_hash_aref(s->rack_fallback, key);
  VALUE required = iodine___upload_required_value(self, s, key);
  if (required != Qundef && !iodine_hmap_count(&s->rack_view.map))
    return required;
  iodine_hmap_node_s *node = iodine_hmap_get_ptr(&s->rack_view.map, key);
  if (node)
    return node->value; /* preserves explicit nil / false overrides */
  return required == Qundef ? Qnil : required;
}

/** Ruby: upload[key] = value (changes only the compatibility view). */
static VALUE iodine_upload_aset(VALUE self, VALUE key, VALUE value) {
  iodine_upload_s *s = iodine_upload_ptr(self);
  if (!iodine___upload_minimap_key_p(key)) {
    if (s->rack_fallback == Qnil)
      s->rack_fallback = rb_hash_new();
    return rb_hash_aset(s->rack_fallback, key, value);
  }
  VALUE required = iodine___upload_required_value(self, s, key);
  if (required == Qundef)
    return iodine_minimap_store(&s->rack_view, key, value);
  /* Required keys are Symbols and nil is an override, not a deletion. */
  iodine_hmap_set(&s->rack_view.map, key, value, NULL);
  return value;
}

/* *****************************************************************************
Params Parser Callbacks (Ruby construction and NeoRack conflict resolution)
***************************************************************************** */

typedef struct {
  iodine_connection_s *c;
  VALUE root;
  uint8_t malformed;
  uint8_t io_error;
} iodine_params_udata_s;

/** Returns the request's memoized params Hash, creating it if needed. */
FIO_SFUNC VALUE iodine___params_hash(iodine_connection_s *c) {
  VALUE h = c->store[IODINE_CONNECTION_STORE_params];
  if (h == Qnil) {
    h = rb_hash_new();
    c->store[IODINE_CONNECTION_STORE_params] = h;
  }
  return h;
}

FIO_SFUNC int iodine___params_is_hash(VALUE value) {
  return RB_TYPE_P(value, RUBY_T_HASH); /* subclasses included; Upload isn't */
}

FIO_SFUNC VALUE iodine___params_key(fio_buf_info_s key) {
  return rb_str_freeze(
      rb_enc_str_new(key.buf, (long)key.len, IodineBinaryEncoding));
}

/* Merge with actual Ruby values, so false and nil never mean "missing". */
FIO_SFUNC void iodine___params_merge_at(VALUE dest, VALUE key, VALUE incoming);

FIO_SFUNC int iodine___params_merge_each(VALUE key, VALUE value, VALUE dest) {
  iodine___params_merge_at(dest, key, value);
  return ST_CONTINUE;
}

FIO_SFUNC void iodine___params_merge_hash(VALUE dest, VALUE incoming) {
  rb_hash_foreach(incoming, iodine___params_merge_each, dest);
}

FIO_SFUNC void iodine___params_merge_at(VALUE dest, VALUE key, VALUE incoming) {
  VALUE current = rb_hash_lookup2(dest, key, Qundef);
  if (current == Qundef) {
    rb_hash_aset(dest, key, incoming);
    return;
  }
  if (iodine___params_is_hash(current) && iodine___params_is_hash(incoming)) {
    iodine___params_merge_hash(current, incoming);
    return;
  }
  if (iodine___params_is_hash(incoming)) {
    /* Keep current rooted in dest until it is nested in the incoming map. */
    iodine___params_merge_at(incoming, IODINE_PARAMS_EMPTY_KEY, current);
    rb_hash_aset(dest, key, incoming);
    return;
  }
  if (iodine___params_is_hash(current)) {
    iodine___params_merge_at(current, IODINE_PARAMS_EMPTY_KEY, incoming);
    return;
  }
  if (RB_TYPE_P(current, RUBY_T_ARRAY) && RB_TYPE_P(incoming, RUBY_T_ARRAY)) {
    rb_ary_concat(current, incoming);
    return;
  }
  if (RB_TYPE_P(current, RUBY_T_ARRAY)) {
    rb_ary_push(current, incoming);
    return;
  }
  if (RB_TYPE_P(incoming, RUBY_T_ARRAY)) {
    rb_ary_unshift(incoming, current);
    rb_hash_aset(dest, key, incoming);
    return;
  }
  VALUE ary = rb_ary_new_capa(2);
  rb_ary_push(ary, current);
  rb_ary_push(ary, incoming);
  rb_hash_aset(dest, key, ary);
}

/* Resolve conflicts in Ruby; return no replaced value to the parser. */
FIO_SFUNC fio_json_push_result_s iodine___params_on_map(void *udata_,
                                                        void *ctx,
                                                        fio_buf_info_s key) {
  iodine_params_udata_s *u = (iodine_params_udata_s *)udata_;
  if (!ctx)
    return (fio_json_push_result_s){.obj = (void *)u->root};
  if (!key.buf) { /* array parent: append */
    VALUE map = rb_hash_new();
    rb_ary_push((VALUE)ctx, map);
    return (fio_json_push_result_s){.obj = (void *)map};
  }
  VALUE k = iodine___params_key(key);
  VALUE current = rb_hash_lookup2((VALUE)ctx, k, Qundef);
  if (current != Qundef && iodine___params_is_hash(current))
    return (fio_json_push_result_s){.obj = (void *)current};
  VALUE map = rb_hash_new();
  if (current != Qundef)
    rb_hash_aset(map, IODINE_PARAMS_EMPTY_KEY, current);
  rb_hash_aset((VALUE)ctx, k, map);
  return (fio_json_push_result_s){.obj = (void *)map};
}

FIO_SFUNC VALUE iodine___params_open_array_at(VALUE dest, VALUE key) {
  VALUE current = rb_hash_lookup2(dest, key, Qundef);
  if (current != Qundef && RB_TYPE_P(current, RUBY_T_ARRAY))
    return current;
  if (current != Qundef && iodine___params_is_hash(current))
    return iodine___params_open_array_at(current, IODINE_PARAMS_EMPTY_KEY);
  VALUE ary = rb_ary_new();
  if (current != Qundef)
    rb_ary_push(ary, current);
  rb_hash_aset(dest, key, ary);
  return ary;
}

FIO_SFUNC fio_json_push_result_s iodine___params_on_array(void *udata_,
                                                          void *ctx,
                                                          fio_buf_info_s key) {
  (void)udata_;
  if (!ctx)
    return (fio_json_push_result_s){
        .err =
            FIO_JSON_ERROR_GENERIC}; /* only JSON objects contribute params */
  if (!key.buf) {
    VALUE ary = rb_ary_new();
    rb_ary_push((VALUE)ctx, ary);
    return (fio_json_push_result_s){.obj = (void *)ary};
  }
  return (fio_json_push_result_s){
      .obj = (void *)iodine___params_open_array_at((VALUE)ctx,
                                                   iodine___params_key(key))};
}

FIO_SFUNC fio_json_obj_result_s iodine___params_map_push(void *udata_,
                                                         void *map,
                                                         fio_buf_info_s key,
                                                         void *value) {
  iodine_params_udata_s *u = (iodine_params_udata_s *)udata_;
  if (!value) {
    /* The STL uses NULL for a missing JSON value; params false is Qundef. */
    u->malformed = 1;
    return (fio_json_obj_result_s){.err = FIO_JSON_ERROR_GENERIC};
  }
  VALUE incoming = (VALUE)value;
  if (incoming == Qundef)
    incoming = Qfalse;
  if (RB_TYPE_P(incoming, RUBY_T_DATA) &&
      rb_typeddata_is_kind_of(incoming, &IODINE_UPLOAD_DATA_TYPE)) {
    iodine_upload_s *upload = (iodine_upload_s *)RTYPEDDATA_DATA(incoming);
    fio_thread_mutex_lock(&upload->lock);
    if (!upload->closed)
      fio_data_seek(&upload->data, 0); /* upload starts at byte zero */
    fio_thread_mutex_unlock(&upload->lock);
  }
  iodine___params_merge_at((VALUE)map, iodine___params_key(key), incoming);
  return (fio_json_obj_result_s){0};
}

FIO_SFUNC int iodine___params_array_push(void *udata_, void *ctx, void *value) {
  iodine_params_udata_s *u = (iodine_params_udata_s *)udata_;
  if (!value) {
    u->malformed = 1;
    return 0;
  }
  VALUE incoming = (VALUE)value;
  if (incoming == Qundef)
    incoming = Qfalse;
  rb_ary_push((VALUE)ctx, incoming);
  return 0;
}

FIO_SFUNC int iodine___params_is_array(void *udata_, void *ctx) {
  (void)udata_;
  return ctx && (VALUE)ctx != Qundef && RB_TYPE_P((VALUE)ctx, RUBY_T_ARRAY);
}

/** Hashes are maps (an Upload is a scalar TypedData "other" value). */
FIO_SFUNC int iodine___params_is_map(void *udata_, void *ctx) {
  (void)udata_;
  return ctx && (VALUE)ctx != Qundef && iodine___params_is_hash((VALUE)ctx);
}

FIO_SFUNC void iodine___params_free_unused_object(void *udata_, void *ctx) {
  (void)udata_;
  VALUE value = (VALUE)ctx;
  if (ctx && RB_TYPE_P(value, RUBY_T_DATA) &&
      rb_typeddata_is_kind_of(value, &IODINE_UPLOAD_DATA_TYPE))
    iodine___upload_close((iodine_upload_s *)RTYPEDDATA_DATA(value));
  /* Other Ruby objects are GC managed. */
}

/** Query / form / multipart-text values: always binary Strings. */
FIO_SFUNC fio_json_obj_result_s
iodine___params_on_string_simple(void *udata_, const void *start, size_t len) {
  (void)udata_;
  return (fio_json_obj_result_s){
      .obj = (void *)rb_enc_str_new((const char *)start,
                                    (long)len,
                                    IodineBinaryEncoding)};
}

FIO_SFUNC fio_json_obj_result_s iodine___params_on_number(void *udata_,
                                                          int64_t i) {
  (void)udata_;
  return (fio_json_obj_result_s){.obj = (void *)LL2NUM((long long)i)};
}

FIO_SFUNC fio_json_obj_result_s iodine___params_on_float(void *udata_,
                                                         double f) {
  (void)udata_;
  return (fio_json_obj_result_s){.obj = (void *)DBL2NUM(f)};
}

FIO_SFUNC fio_json_obj_result_s iodine___params_on_false(void *udata_) {
  (void)udata_;
  return (fio_json_obj_result_s){
      .obj =
          (void *)Qundef}; /* non-NULL across the STL; decoded on insertion */
}

/* Unescape directly into a Ruby String. This avoids a temporary C allocation,
   checks the writer result, and gives every escaped string one Ruby owner. */
FIO_SFUNC fio_json_obj_result_s iodine___params_on_string(void *udata_,
                                                          const void *start,
                                                          size_t len) {
  iodine_params_udata_s *u = (iodine_params_udata_s *)udata_;
  if (len > LONG_MAX) {
    u->malformed = 1;
    return (fio_json_obj_result_s){.err = FIO_JSON_ERROR_GENERIC};
  }
  VALUE str = rb_str_new(NULL, (long)len);
  fio_str_info_s dest = FIO_STR_INFO3(RSTRING_PTR(str), 0, len + 1);
  if (fio_string_write_unescape(&dest, NULL, start, len)) {
    u->malformed = 1;
    return (fio_json_obj_result_s){.err = FIO_JSON_ERROR_GENERIC};
  }
  rb_str_set_len(str, (long)dest.len);
  return (fio_json_obj_result_s){.obj = (void *)str};
}

/* Multipart file uploads. */

FIO_SFUNC void *iodine___params_on_file(void *udata_,
                                        fio_str_info_s name,
                                        fio_str_info_s filename,
                                        fio_str_info_s content_type,
                                        fio_str_info_s headers) {
  iodine_params_udata_s *u = (iodine_params_udata_s *)udata_;
  VALUE upload = rb_obj_alloc(iodine_rb_BASE_UPLOAD); /* open at byte zero */
  iodine_upload_s *s = iodine_upload_ptr(upload);
  u->c->store[IODINE_CONNECTION_STORE_tmp] = upload; /* active parser root */
  iodine___upload_track(u->c, s);
  s->filename =
      rb_enc_str_new(filename.buf, (long)filename.len, IodineBinaryEncoding);
  s->name = rb_enc_str_new(name.buf, (long)name.len, IodineBinaryEncoding);
  s->type = content_type.buf ? rb_enc_str_new(content_type.buf,
                                              (long)content_type.len,
                                              IodineBinaryEncoding)
                             : Qnil;
  s->headers =
      headers.buf
          ? rb_enc_str_new(headers.buf, (long)headers.len, IodineBinaryEncoding)
          : Qnil;
  return (void *)upload;
}

FIO_SFUNC int iodine___params_on_file_data(void *udata_,
                                           void *file_ctx,
                                           fio_buf_info_s data) {
  iodine_params_udata_s *u = (iodine_params_udata_s *)udata_;
  iodine_upload_s *s = (iodine_upload_s *)RTYPEDDATA_DATA((VALUE)file_ctx);
  fio_thread_mutex_lock(&s->lock);
  int result = s->closed ? -1 : fio_data_write(&s->data, data.buf, data.len);
  fio_thread_mutex_unlock(&s->lock);
  if (result) {
    u->io_error = 1;
    iodine___upload_close(s); /* a partial failed write is never committed */
    return -1;
  }
  return 0;
}

/* *****************************************************************************
Exception-safe callback boundary

No Ruby exception may unwind through the STL parser: its local decoded strings,
JSON keys and multipart pending buffers need normal C cleanup. Each allocating
callback uses rb_protect, records the first exception, and stops the parser.
***************************************************************************** */

enum iodine___params_callback_e {
  IODINE_PARAMS_CB_NUMBER,
  IODINE_PARAMS_CB_FLOAT,
  IODINE_PARAMS_CB_STRING,
  IODINE_PARAMS_CB_STRING_SIMPLE,
  IODINE_PARAMS_CB_MAP,
  IODINE_PARAMS_CB_ARRAY,
  IODINE_PARAMS_CB_MAP_PUSH,
  IODINE_PARAMS_CB_ARRAY_PUSH,
  IODINE_PARAMS_CB_FILE,
};

typedef struct {
  iodine_params_udata_s *u;
  enum iodine___params_callback_e op;
  void *ctx;
  void *value;
  void *old;
  void *result;
  fio_buf_info_s key;
  const void *start;
  size_t len;
  int64_t integer;
  double floating;
  int status;
  fio_str_info_s name;
  fio_str_info_s filename;
  fio_str_info_s content_type;
  fio_str_info_s headers;
  fio_buf_info_s data;
} iodine___params_callback_args_s;

FIO_SFUNC VALUE iodine___params_callback_dispatch(VALUE args_) {
  iodine___params_callback_args_s *a = (iodine___params_callback_args_s *)args_;
  switch (a->op) {
  case IODINE_PARAMS_CB_NUMBER: {
    fio_json_obj_result_s r = iodine___params_on_number(a->u, a->integer);
    a->result = r.obj;
    a->status = r.err;
  } break;
  case IODINE_PARAMS_CB_FLOAT: {
    fio_json_obj_result_s r = iodine___params_on_float(a->u, a->floating);
    a->result = r.obj;
    a->status = r.err;
  } break;
  case IODINE_PARAMS_CB_STRING: {
    fio_json_obj_result_s r = iodine___params_on_string(a->u, a->start, a->len);
    a->result = r.obj;
    a->status = r.err;
  } break;
  case IODINE_PARAMS_CB_STRING_SIMPLE: {
    fio_json_obj_result_s r =
        iodine___params_on_string_simple(a->u, a->start, a->len);
    a->result = r.obj;
    a->status = r.err;
  } break;
  case IODINE_PARAMS_CB_MAP: {
    fio_json_push_result_s r = iodine___params_on_map(a->u, a->ctx, a->key);
    a->result = r.obj;
    a->old = r.old;
    a->status = r.err;
  } break;
  case IODINE_PARAMS_CB_ARRAY: {
    fio_json_push_result_s r = iodine___params_on_array(a->u, a->ctx, a->key);
    a->result = r.obj;
    a->old = r.old;
    a->status = r.err;
  } break;
  case IODINE_PARAMS_CB_MAP_PUSH: {
    fio_json_obj_result_s r =
        iodine___params_map_push(a->u, a->ctx, a->key, a->value);
    a->result = r.obj;
    a->status = r.err;
  } break;
  case IODINE_PARAMS_CB_ARRAY_PUSH:
    a->status = iodine___params_array_push(a->u, a->ctx, a->value);
    break;
  case IODINE_PARAMS_CB_FILE:
    a->result = iodine___params_on_file(a->u,
                                        a->name,
                                        a->filename,
                                        a->content_type,
                                        a->headers);
    if (iodine___params_on_file_data(a->u, a->result, a->data)) {
      a->result = NULL;
      a->status = -1;
    }
    break;
  }
  return Qnil;
}

FIO_SFUNC void iodine___params_callback_protect(
    iodine___params_callback_args_s *a) {
  VALUE exception = a->u->c->store[IODINE_CONNECTION_STORE_params_exception];
  if (exception == Qnil) {
    int state = 0;
    rb_protect(iodine___params_callback_dispatch, (VALUE)a, &state);
    if (!state)
      return;
    exception = rb_errinfo();
    a->u->c->store[IODINE_CONNECTION_STORE_params_exception] = exception;
    rb_set_errinfo(Qnil);
  }
  a->result = NULL;
  a->status = -1;
}

#define IODINE_PARAMS_CALLBACK_WRAPPER_VOID(name, operation)                   \
  FIO_SFUNC fio_json_push_result_s name(void *udata_,                          \
                                        void *ctx,                             \
                                        fio_buf_info_s key) {                  \
    iodine___params_callback_args_s a = {.u = (iodine_params_udata_s *)udata_, \
                                         .op = operation,                      \
                                         .ctx = ctx,                           \
                                         .key = key};                          \
    iodine___params_callback_protect(&a);                                      \
    return (fio_json_push_result_s){.obj = a.result,                           \
                                    .old = a.old,                              \
                                    .err = a.status};                          \
  }

IODINE_PARAMS_CALLBACK_WRAPPER_VOID(iodine___params_cb_on_map,
                                    IODINE_PARAMS_CB_MAP)
IODINE_PARAMS_CALLBACK_WRAPPER_VOID(iodine___params_cb_on_array,
                                    IODINE_PARAMS_CB_ARRAY)
#undef IODINE_PARAMS_CALLBACK_WRAPPER_VOID

FIO_SFUNC fio_json_obj_result_s iodine___params_cb_on_number(void *udata_,
                                                             int64_t i) {
  iodine___params_callback_args_s a = {
      .u = (iodine_params_udata_s *)udata_,
      .op = IODINE_PARAMS_CB_NUMBER,
      .integer = i,
  };
  iodine___params_callback_protect(&a);
  return (fio_json_obj_result_s){.obj = a.result, .err = a.status};
}

FIO_SFUNC fio_json_obj_result_s iodine___params_cb_on_float(void *udata_,
                                                            double f) {
  iodine___params_callback_args_s a = {
      .u = (iodine_params_udata_s *)udata_,
      .op = IODINE_PARAMS_CB_FLOAT,
      .floating = f,
  };
  iodine___params_callback_protect(&a);
  return (fio_json_obj_result_s){.obj = a.result, .err = a.status};
}

FIO_SFUNC fio_json_obj_result_s
iodine___params_cb_on_string(void *udata_, fio_buf_info_s str) {
  iodine___params_callback_args_s a = {
      .u = (iodine_params_udata_s *)udata_,
      .op = IODINE_PARAMS_CB_STRING,
      .start = str.buf,
      .len = str.len,
  };
  iodine___params_callback_protect(&a);
  return (fio_json_obj_result_s){.obj = a.result, .err = a.status};
}

FIO_SFUNC fio_json_obj_result_s
iodine___params_cb_on_string_simple(void *udata_, fio_buf_info_s str) {
  iodine___params_callback_args_s a = {
      .u = (iodine_params_udata_s *)udata_,
      .op = IODINE_PARAMS_CB_STRING_SIMPLE,
      .start = str.buf,
      .len = str.len,
  };
  iodine___params_callback_protect(&a);
  return (fio_json_obj_result_s){.obj = a.result, .err = a.status};
}

FIO_SFUNC fio_json_obj_result_s iodine___params_cb_map_push(void *udata_,
                                                            void *map,
                                                            fio_buf_info_s key,
                                                            void *value) {
  iodine___params_callback_args_s a = {
      .u = (iodine_params_udata_s *)udata_,
      .op = IODINE_PARAMS_CB_MAP_PUSH,
      .ctx = map,
      .key = key,
      .value = value,
  };
  iodine___params_callback_protect(&a);
  return (fio_json_obj_result_s){.obj = a.result, .err = a.status};
}

FIO_SFUNC int iodine___params_cb_array_push(void *udata_,
                                            void *ctx,
                                            void *value) {
  iodine___params_callback_args_s a = {
      .u = (iodine_params_udata_s *)udata_,
      .op = IODINE_PARAMS_CB_ARRAY_PUSH,
      .ctx = ctx,
      .value = value,
  };
  iodine___params_callback_protect(&a);
  return a.status;
}

FIO_SFUNC fio_json_obj_result_s iodine___params_cb_on_file(
    void *udata_,
    fio_str_info_s name,
    fio_str_info_s filename,
    fio_str_info_s content_type,
    fio_str_info_s headers,
    fio_buf_info_s data) {
  iodine___params_callback_args_s a = {
      .u = (iodine_params_udata_s *)udata_,
      .op = IODINE_PARAMS_CB_FILE,
      .name = name,
      .filename = filename,
      .content_type = content_type,
      .headers = headers,
      .data = data,
  };
  iodine___params_callback_protect(&a);
  return (fio_json_obj_result_s){.obj = a.result, .err = a.status};
}

/* The STL treats this callback table as read-only. */
static fio_http_params_parse_callbacks_s IODINE_PARAMS_CALLBACKS = {
    .on_null = iodine___json_on_null,
    .on_true = iodine___json_on_true,
    .on_false = iodine___params_on_false,
    .on_number = iodine___params_cb_on_number,
    .on_float = iodine___params_cb_on_float,
    .on_string = iodine___params_cb_on_string,
    .on_string_simple = iodine___params_cb_on_string_simple,
    .on_map = iodine___params_cb_on_map,
    .on_array = iodine___params_cb_on_array,
    .map_push = iodine___params_cb_map_push,
    .array_push = iodine___params_cb_array_push,
    .is_array = iodine___params_is_array,
    .is_map = iodine___params_is_map,
    .free_unused_object = iodine___params_free_unused_object,
    .on_file = iodine___params_cb_on_file,
};

/* *****************************************************************************
Content-Type, Field Budget, and Parse Guard
***************************************************************************** */

enum {
  IODINE_PARAMS_BODY_NONE = 0,
  IODINE_PARAMS_BODY_JSON,
  IODINE_PARAMS_BODY_URLENCODED,
  IODINE_PARAMS_BODY_MULTIPART
};

FIO_SFUNC int iodine___params_media_eq(fio_str_info_s value,
                                       const char *lower,
                                       size_t len) {
  if (value.len != len)
    return 0;
  for (size_t i = 0; i < len; ++i)
    if (((unsigned char)value.buf[i] | 0x20) != (unsigned char)lower[i])
      return 0;
  return 1;
}

/* Length-delimited detection has no fixed media-token buffer or truncation. */
FIO_SFUNC int iodine___params_body_type(fio_http_s *h) {
  fio_str_info_s ct =
      fio_http_request_header(h, FIO_STR_INFO2((char *)"content-type", 12), 0);
  if (!ct.buf)
    return IODINE_PARAMS_BODY_NONE;
  while (ct.len && (*ct.buf == ' ' || *ct.buf == '\t')) {
    ++ct.buf;
    --ct.len;
  }
  size_t len = 0;
  while (len < ct.len && ct.buf[len] != ';' && ct.buf[len] != ' ' &&
         ct.buf[len] != '\t')
    ++len;
  ct.len = len;
  if (iodine___params_media_eq(ct, "application/x-www-form-urlencoded", 33))
    return IODINE_PARAMS_BODY_URLENCODED;
  if (iodine___params_media_eq(ct, "multipart/form-data", 19))
    return IODINE_PARAMS_BODY_MULTIPART;
  if (iodine___params_media_eq(ct, "application/json", 16) ||
      iodine___params_media_eq(ct, "text/json", 9) ||
      iodine___params_media_eq(ct, "application/x-json", 18))
    return IODINE_PARAMS_BODY_JSON;
  if (ct.len > 17 &&
      iodine___params_media_eq(FIO_STR_INFO2(ct.buf, 12), "application/", 12) &&
      iodine___params_media_eq(FIO_STR_INFO2(ct.buf + ct.len - 5, 5),
                               "+json",
                               5))
    return IODINE_PARAMS_BODY_JSON;
  return IODINE_PARAMS_BODY_NONE;
}

FIO_SFUNC size_t iodine___params_limit(fio_http_s *h) {
  fio_http_settings_s *s = fio_http_settings(h);
  return (s && s->max_params) ? s->max_params : FIO_HTTP_PARAMS_MAX_FIELDS;
}

FIO_SFUNC size_t iodine___params_remaining(iodine_connection_s *c) {
  const size_t limit = iodine___params_limit(c->http);
  return c->params_spent >= limit ? 0 : limit - c->params_spent;
}

/* Charge exactly what the parser counted. Returns true only for proven excess.
   Saturating after rejection keeps later rescued calls within size_t. */
FIO_SFUNC int iodine___params_charge(iodine_connection_s *c,
                                     fio_http_params_parse_result_s r,
                                     size_t remaining) {
  if (r.count > remaining) {
    c->params_spent = iodine___params_limit(c->http);
    return 1;
  }
  c->params_spent += r.count;
  return 0;
}

typedef struct {
  iodine_connection_s *c;
  iodine_params_udata_s u;
  VALUE previous_tmp;
  VALUE previous_stage;
  VALUE previous_input;
  VALUE previous_exception;
  size_t body_position;
  size_t remaining;
  int body_type;
  uint8_t restore_body;
} iodine___params_parse_s;

FIO_SFUNC VALUE iodine___params_parse_ensure(VALUE context_) {
  iodine___params_parse_s *p = (iodine___params_parse_s *)context_;
  iodine_connection_s *c = p->c;
  c->store[IODINE_CONNECTION_STORE_tmp] = p->previous_tmp;
  c->store[IODINE_CONNECTION_STORE_params_stage] = p->previous_stage;
  c->store[IODINE_CONNECTION_STORE_params_input] = p->previous_input;
  c->store[IODINE_CONNECTION_STORE_params_exception] = p->previous_exception;
  if (p->restore_body && c->http)
    fio_http_body_seek(c->http, (ssize_t)p->body_position);
  return Qnil;
}

FIO_SFUNC void iodine___params_parse_guard(iodine___params_parse_s *p,
                                           iodine_connection_s *c,
                                           int restore_body) {
  iodine_connection_s *connection = c;
  p->c = connection;
  p->previous_tmp = connection->store[IODINE_CONNECTION_STORE_tmp];
  p->previous_stage = connection->store[IODINE_CONNECTION_STORE_params_stage];
  p->previous_input = connection->store[IODINE_CONNECTION_STORE_params_input];
  p->previous_exception =
      connection->store[IODINE_CONNECTION_STORE_params_exception];
  p->restore_body = !!restore_body;
  if (restore_body)
    p->body_position = fio_http_body_pos(connection->http);
  connection->store[IODINE_CONNECTION_STORE_tmp] = Qnil;
  connection->store[IODINE_CONNECTION_STORE_params_stage] = Qnil;
  connection->store[IODINE_CONNECTION_STORE_params_input] = Qnil;
  connection->store[IODINE_CONNECTION_STORE_params_exception] = Qnil;
}

FIO_SFUNC void iodine___params_reraise(iodine_connection_s *c) {
  VALUE exception = c->store[IODINE_CONNECTION_STORE_params_exception];
  if (exception != Qnil)
    rb_exc_raise(exception);
}

FIO_SFUNC void iodine___params_raise_overflow(iodine_connection_s *c) {
  rb_raise(rb_eRangeError,
           "request params field limit exceeded (%zu)",
           iodine___params_limit(c->http));
}

FIO_SFUNC VALUE iodine___params_query_parse(VALUE context_) {
  iodine___params_parse_s *p = (iodine___params_parse_s *)context_;
  const size_t parse_limit =
      p->remaining == SIZE_MAX ? SIZE_MAX : p->remaining + 1;
  fio_http_params_parse_result_s r =
      fio_http_query_parse(p->c->http,
                           .callbacks = &IODINE_PARAMS_CALLBACKS,
                           .udata = &p->u,
                           .limit = parse_limit);
  int overflow = iodine___params_charge(p->c, r, p->remaining);
  iodine___params_reraise(p->c);
  if (overflow)
    iodine___params_raise_overflow(p->c);
  if (r.err) {
    /* URL decoding keeps malformed escapes literal; remaining errors are
       name-depth / allocation resource failures, not field malformation. */
    rb_raise(rb_eRangeError, "request params nesting or parser limit exceeded");
  }
  return p->u.root;
}

FIO_SFUNC int iodine___params_json_object_prefix(fio_str_info_s body) {
  size_t i = 0;
  if (body.len >= 3 && (unsigned char)body.buf[0] == 0xEF &&
      (unsigned char)body.buf[1] == 0xBB && (unsigned char)body.buf[2] == 0xBF)
    i = 3;
  while (i < body.len && (body.buf[i] == ' ' || body.buf[i] == '\t' ||
                          body.buf[i] == '\r' || body.buf[i] == '\n'))
    ++i;
  return i < body.len && body.buf[i] == '{';
}

FIO_SFUNC VALUE iodine___params_body_parse(VALUE context_) {
  iodine___params_parse_s *p = (iodine___params_parse_s *)context_;
  VALUE destination = iodine___params_hash(p->c);
  p->u.root = rb_hash_new();
  p->c->store[IODINE_CONNECTION_STORE_params_stage] = p->u.root;
  fio_http_params_parse_result_s r = {.result = (void *)p->u.root};

  if (p->body_type == IODINE_PARAMS_BODY_JSON) {
    const size_t len = fio_http_body_length(p->c->http);
    fio_http_body_seek(p->c->http, 0);
    fio_str_info_s body = fio_http_body_read(p->c->http, len);
    if (body.len != len || (len && !body.buf))
      rb_raise(rb_eIOError, "request body couldn't be read for JSON parsing");
    if (!iodine___params_json_object_prefix(body))
      return destination; /* non-object roots allocate no Ruby value tree */
    VALUE input = rb_str_new(body.buf, (long)len); /* includes a NUL guard */
    p->c->store[IODINE_CONNECTION_STORE_params_input] = input;
    fio_json_result_s json =
        fio_json_parse((fio_json_parser_callbacks_s *)&IODINE_PARAMS_CALLBACKS,
                       &p->u,
                       (void *)p->u.root,
                       RSTRING_PTR(input),
                       len);
    /* Preserve the public JSON error; all JSON parse errors discard the stage.
     */
    r.err = (fio_http_params_parse_error_e)json.err;
    r.consumed = json.stop_pos;
    size_t i = json.stop_pos;
    while (i < len &&
           (RSTRING_PTR(input)[i] == ' ' || RSTRING_PTR(input)[i] == '\t' ||
            RSTRING_PTR(input)[i] == '\r' || RSTRING_PTR(input)[i] == '\n'))
      ++i;
    if (i != len)
      p->u.malformed = 1; /* reject trailing non-whitespace contribution */
  } else {
    const size_t parse_limit =
        p->remaining == SIZE_MAX ? SIZE_MAX : p->remaining + 1;
    r = fio_http_body_parse(p->c->http,
                            .callbacks = &IODINE_PARAMS_CALLBACKS,
                            .udata = &p->u,
                            .limit = parse_limit);
  }

  int overflow = p->body_type == IODINE_PARAMS_BODY_JSON
                     ? 0
                     : iodine___params_charge(p->c, r, p->remaining);
  iodine___params_reraise(p->c);
  if (overflow)
    iodine___params_raise_overflow(p->c);
  if (p->u.io_error)
    rb_raise(rb_eIOError, "upload storage failed while parsing request body");
  if (p->body_type != IODINE_PARAMS_BODY_JSON) {
    if (r.err == FIO_HTTP_PARAMS_PARSE_IO)
      rb_raise(rb_eIOError, "request body couldn't be read for params parsing");
    if (r.err == FIO_HTTP_PARAMS_PARSE_DEPTH ||
        (r.err == FIO_HTTP_PARAMS_PARSE_ERROR &&
         p->body_type != IODINE_PARAMS_BODY_MULTIPART))
      rb_raise(rb_eRangeError,
               "request params nesting or parser limit exceeded");
  }
  if (!r.err && !p->u.malformed)
    iodine___params_merge_hash(destination, p->u.root);
  /* JSON and multipart failures discard the whole staged contribution. Custom
     JSON callback integers cannot be mistaken for HTTP IO / depth failures. */
  return destination;
}

/* *****************************************************************************
Request-End Cleanup (called from iodine_connection.h lifecycle callbacks)
***************************************************************************** */

static void *iodine___params_cleanup_in_gvl(void *c_) {
  iodine_connection_s *c = (iodine_connection_s *)c_;
  while (c->open_uploads)
    iodine___upload_close(c->open_uploads);
  fio_atomic_and(&c->flags, ~IODINE_CONNECTION_PARAMS_HAS_UPLOAD);
  return NULL;
}

/* The sticky flag avoids GVL acquisition for requests that never had uploads.
   The head itself is read and drained only with the GVL. */
FIO_SFUNC void iodine_params_on_finish(iodine_connection_s *c) {
  if (!(fio_atomic_or(&c->flags, 0) & IODINE_CONNECTION_PARAMS_HAS_UPLOAD))
    return;
  iodine_c_call_with(iodine___params_cleanup_in_gvl, c);
}

/* Connection dfree runs with the GVL. Drain defensively so a later Upload
   finalizer can never unlink through a pointer into freed Connection memory. */
FIO_SFUNC void iodine_params_on_connection_free(iodine_connection_s *c) {
  iodine___params_cleanup_in_gvl(c);
}

/* *****************************************************************************
Iodine::Connection Methods (NeoRack `params` extension v0.0.1)
***************************************************************************** */

/** Parses e.query once into the request's memoized params Hash. */
FIO_SFUNC VALUE iodine_connection_params_query2params(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  VALUE hash = iodine___params_hash(c);
  if (!c->http || (c->flags & IODINE_CONNECTION_PARAMS_QUERY))
    return hash;
  c->flags |= IODINE_CONNECTION_PARAMS_QUERY; /* merge once, even on error */
  iodine___params_parse_s p = {
      .u = {.c = c, .root = hash},
      .remaining = iodine___params_remaining(c),
  };
  iodine___params_parse_guard(&p, c, 0);
  return rb_ensure(iodine___params_query_parse,
                   (VALUE)&p,
                   iodine___params_parse_ensure,
                   (VALUE)&p);
}

/** Parses a supported request body once, preserving body position throughout.
 */
FIO_SFUNC VALUE iodine_connection_params_body2params(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  VALUE hash = iodine___params_hash(c);
  if (!c->http || (c->flags & IODINE_CONNECTION_PARAMS_BODY))
    return hash;
  c->flags |= IODINE_CONNECTION_PARAMS_BODY;
  const int type = iodine___params_body_type(c->http);
  if (type == IODINE_PARAMS_BODY_NONE || !fio_http_body_length(c->http))
    return hash; /* unknown / absent Content-Type, or no body: not parsed */
  iodine___params_parse_s p = {
      .u = {.c = c, .root = Qnil},
      .remaining = iodine___params_remaining(c),
      .body_type = type,
  };
  iodine___params_parse_guard(&p, c, 1);
  return rb_ensure(iodine___params_body_parse,
                   (VALUE)&p,
                   iodine___params_parse_ensure,
                   (VALUE)&p);
}

/** Returns an existing Hash unchanged; otherwise calls query then body. */
FIO_SFUNC VALUE iodine_connection_params(VALUE o) {
  iodine_connection_s *c = iodine_connection_ptr(o);
  if (c->store[IODINE_CONNECTION_STORE_params] != Qnil || !c->http)
    return iodine___params_hash(c);
  iodine_connection_params_query2params(o);
  return iodine_connection_params_body2params(o);
}

/* *****************************************************************************
Init
***************************************************************************** */

static void Init_Iodine_Params(void) {
  iodine_rb_BASE_UPLOAD =
      rb_define_class_under(iodine_rb_IODINE_BASE, "Upload", rb_cObject);
  rb_define_alloc_func(iodine_rb_BASE_UPLOAD, iodine_upload_alloc);
  rb_define_method(iodine_rb_BASE_UPLOAD, "read", iodine_upload_read, -1);
  rb_define_method(iodine_rb_BASE_UPLOAD, "rewind", iodine_upload_rewind, 0);
  rb_define_method(iodine_rb_BASE_UPLOAD, "size", iodine_upload_size, 0);
  rb_define_method(iodine_rb_BASE_UPLOAD, "save", iodine_upload_save, 1);
  rb_define_method(iodine_rb_BASE_UPLOAD, "close", iodine_upload_close, 0);
  rb_define_method(iodine_rb_BASE_UPLOAD, "closed?", iodine_upload_closed_p, 0);
  rb_define_method(iodine_rb_BASE_UPLOAD,
                   "filename",
                   iodine_upload_filename,
                   0);
  rb_define_method(iodine_rb_BASE_UPLOAD, "type", iodine_upload_type, 0);
  rb_define_method(iodine_rb_BASE_UPLOAD, "name", iodine_upload_name, 0);
  rb_define_method(iodine_rb_BASE_UPLOAD, "headers", iodine_upload_headers, 0);
  rb_define_method(iodine_rb_BASE_UPLOAD, "[]", iodine_upload_aref, 1);
  rb_define_method(iodine_rb_BASE_UPLOAD, "[]=", iodine_upload_aset, 2);
  STORE.hold(iodine_rb_BASE_UPLOAD);

  IODINE_PARAMS_EMPTY_KEY = rb_str_freeze(rb_str_new("", 0));
  STORE.hold(IODINE_PARAMS_EMPTY_KEY);
  IODINE_CONST_ID_STORE(IODINE_PARAMS_FILENAME_ID, "filename");
  IODINE_CONST_ID_STORE(IODINE_PARAMS_TYPE_ID, "type");
  IODINE_CONST_ID_STORE(IODINE_PARAMS_NAME_ID, "name");
  IODINE_CONST_ID_STORE(IODINE_PARAMS_TEMPFILE_ID, "tempfile");
  IODINE_CONST_ID_STORE(IODINE_PARAMS_HEAD_ID, "head");

  rb_define_method(iodine_rb_IODINE_CONNECTION,
                   "params",
                   iodine_connection_params,
                   0);
  rb_define_method(iodine_rb_IODINE_CONNECTION,
                   "query2params",
                   iodine_connection_params_query2params,
                   0);
  rb_define_method(iodine_rb_IODINE_CONNECTION,
                   "body2params",
                   iodine_connection_params_body2params,
                   0);
}

#endif /* H___IODINE_PARAMS___H */
