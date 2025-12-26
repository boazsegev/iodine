#ifndef H___IODINE___H
#define H___IODINE___H
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/intern.h>
#include <ruby/io.h>
#include <ruby/thread.h>

typedef pid_t fio_thread_pid_t;
typedef VALUE fio_thread_t;

/* *****************************************************************************
Constants
***************************************************************************** */

static ID IODINE_CALL_ID;
static ID IODINE_CLOSE_ID;
static ID IODINE_EACH_ID;
static ID IODINE_FILENO_ID;
static ID IODINE_NEW_ID;
static ID IODINE_TO_PATH_ID;
static ID IODINE_TO_S_ID;
static ID IODINE_TO_JSON_ID;
static ID IODINE_RACK_HIJACK_ID;
static ID IODINE_ON_AUTHENTICATE_ID;
static ID IODINE_ON_AUTHENTICATE_SSE_ID;
static ID IODINE_ON_AUTHENTICATE_WEBSOCKET_ID;
static ID IODINE_ON_CLOSE_ID;
static ID IODINE_ON_DATA_ID;
static ID IODINE_ON_DRAINED_ID;
static ID IODINE_ON_EVENTSOURCE_ID;
static ID IODINE_ON_EVENTSOURCE_RECONNECT_ID;
static ID IODINE_ON_FINISH_ID;
static ID IODINE_ON_HTTP_ID;
static ID IODINE_ON_MESSAGE_ID;
static ID IODINE_ON_OPEN_ID;
static ID IODINE_ON_SHUTDOWN_ID;
static ID IODINE_ON_TIMEOUT_ID;

static ID IODINE_INDEX_ID;
static ID IODINE_SHOW_ID;
// static ID IODINE_NEW_ID;
static ID IODINE_EDIT_ID;
static ID IODINE_CREATE_ID;
static ID IODINE_UPDATE_ID;
static ID IODINE_DELETE_ID;

#define IODINE_CONST_ID_STORE(name, value)                                     \
  name = rb_intern(value);                                                     \
  STORE.hold(RB_ID2SYM(name));

static VALUE iodine_rb_IODINE;
static VALUE iodine_rb_IODINE_BASE;
static VALUE iodine_rb_IODINE_BASE_APP404;
static VALUE iodine_rb_IODINE_CONNECTION;
static VALUE iodine_rb_IODINE_PUBSUB;
static VALUE iodine_rb_IODINE_PUBSUB_ENG;
static VALUE iodine_rb_IODINE_PUBSUB_MSG;
static rb_encoding *IodineUTF8Encoding;
static rb_encoding *IodineBinaryEncoding;

static VALUE IODINE_CONNECTION_ENV_TEMPLATE = Qnil;
static VALUE IODINE_RACK_PROTOCOL_STR;
static VALUE IODINE_RACK_HIJACK_ID_SYM;
static VALUE IODINE_RACK_HIJACK_STR;
static VALUE IODINE_RACK_HIJACK_SYM;
static VALUE IODINE_RACK_UPGRADE_STR;
static VALUE IODINE_RACK_UPGRADE_Q_STR;
static VALUE IODINE_RACK_UPGRADE_WS_SYM;
static VALUE IODINE_RACK_UPGRADE_SSE_SYM;
static VALUE IODINE_RACK_AFTER_RPLY_STR;

#ifndef IODINE_RAW_ON_DATA_READ_BUFFER
#define IODINE_RAW_ON_DATA_READ_BUFFER (1ULL << 14)
#endif

#define IODINE_STORE_IS_SKIP(o)                                                \
  (!o || o == Qnil || o == Qtrue || o == Qfalse || TYPE(o) == RUBY_T_FIXNUM)

#define IODINE_RSTR_INFO(o)                                                    \
  { .buf = RSTRING_PTR(o), .len = (size_t)RSTRING_LEN(o) }

/* shadow exit function and route it to Ruby */
#define exit(status) rb_exit(status)

/* *****************************************************************************
facil.io
***************************************************************************** */
#define FIO_LEAK_COUNTER            1
#define FIO_MUSTACHE_LAMBDA_SUPPORT 1
#define FIO_THREADS_BYO             1
#define FIO_THREADS_FORK_BYO        1
#define FIO_MEMORY_ARENA_COUNT_MAX  4
#define FIO_EVERYTHING

#ifndef DEBUG
#undef FIO_LEAK_COUNTER_SKIP_EXIT
#define FIO_LEAK_COUNTER_SKIP_EXIT 1
/* Ruby doesn't always free everything pre-cleanup, so no point in counting */
#undef FIO_LEAK_COUNTER
#define FIO_LEAK_COUNTER 0
#endif

#include "fio-stl.h"

/* Include Redis module (requires FIOBJ types from FIO_EVERYTHING) */
#define FIO_REDIS
#define FIO_FIOBJ
#include "fio-stl.h"

#ifndef DEBUG
#endif

/* *****************************************************************************
Deferring Ruby Blocks
***************************************************************************** */
static void iodine_defer_performe_once(void *block, void *ignr);

#define IODINE_DEFER_BLOCK(blk)                                                \
  do {                                                                         \
    STORE.hold((blk));                                                         \
    fio_io_async(&IODINE_THREAD_POOL,                                          \
                 iodine_defer_performe_once,                                   \
                 (void *)(blk));                                               \
  } while (0)

/* *****************************************************************************
Leak Counters
***************************************************************************** */
FIO_LEAK_COUNTER_DEF(iodine_connection)
FIO_LEAK_COUNTER_DEF(iodine_minimap)
FIO_LEAK_COUNTER_DEF(iodine_mustache)
FIO_LEAK_COUNTER_DEF(iodine_pubsub_msg)
// FIO_LEAK_COUNTER_DEF(iodine_pubsub_eng) /* Ruby doesn't free */
FIO_LEAK_COUNTER_DEF(iodine_hmap)

/* *****************************************************************************
Common Iodine Helpers
***************************************************************************** */

/* IO reactor thread-pool */
static fio_io_async_s IODINE_THREAD_POOL;

static VALUE iodine_handler_method_injection__inner(VALUE self,
                                                    VALUE handler,
                                                    bool is_middleware);

/* layer 1 helpers */
#include "iodine_arg_helper.h"
#include "iodine_store.h"
/* layer 2 helpers */
#include "iodine_caller.h"
#include "iodine_json.h"
#include "iodine_listener.h"
#include "iodine_pubsub_msg.h"
#include "iodine_utils.h"
/* layer 1 modules */
#include "iodine_cli.h"
#include "iodine_defer.h"
#include "iodine_minimap.h"
/* layer 2 modules */
#include "iodine_mustache.h"
#include "iodine_pubsub_eng.h"
#include "iodine_redis.h"
#include "iodine_threads.h"
#include "iodine_tls.h"
/* layer 3 modules */
#include "iodine_connection.h"

/* layer 4 modules */
#include "iodine_core.h"

#endif /* H___IODINE___H */
