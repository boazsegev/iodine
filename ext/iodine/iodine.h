#ifndef H___IODINE___H
#define H___IODINE___H
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/intern.h>
#include <ruby/thread.h>

typedef int fio_thread_pid_t;
typedef VALUE fio_thread_t;

/* *****************************************************************************
Constants
***************************************************************************** */

static ID IODINE_CALL_ID;
static ID IODINE_NEW_ID;
static ID IODINE_ON_ATTACH_ID;
static ID IODINE_ON_AUTHENTICATE_SSE_ID;
static ID IODINE_ON_AUTHENTICATE_WEBSOCKET_ID;
static ID IODINE_ON_CLOSE_ID;
static ID IODINE_ON_DATA_ID;
static ID IODINE_ON_EVENTSOURCE_ID;
static ID IODINE_ON_EVENTSOURCE_RECONNECT_ID;
static ID IODINE_ON_FINISH_ID;
static ID IODINE_ON_HTTP_ID;
static ID IODINE_ON_MESSAGE_ID;
static ID IODINE_ON_OPEN_ID;
static ID IODINE_ON_READY_ID;
static ID IODINE_ON_SHUTDOWN_ID;
static ID IODINE_ON_TIMEOUT_ID;
static ID IODINE_PRE_HTTP_BODY_ID;

#define IODINE_CONST_ID_STORE(name, value)                                     \
  name = rb_intern(value);                                                     \
  STORE.hold(name);

static VALUE iodine_rb_IODINE;
static VALUE iodine_rb_IODINE_BASE;
static VALUE iodine_rb_IODINE_PUBSUB;
static rb_encoding *IodineUTF8Encoding;
static rb_encoding *IodineBinaryEncoding;

#ifndef IODINE_DEFAULT_ON_DATA_READ_BUFFER
#define IODINE_DEFAULT_ON_DATA_READ_BUFFER (1ULL << 14)
#endif

/* *****************************************************************************
facil.io
***************************************************************************** */

#define FIO_LEAK_COUNTER            1
#define FIO_MUSTACHE_LAMBDA_SUPPORT 1
#define FIO_THREADS_BYO             1
#define FIO_THREADS_FORK_BYO        1
#define FIO_EVERYTHING
#include "fio-stl.h"
// #define FIO_FIOBJ
// #include "fio-stl.h"

/* *****************************************************************************
Common Iodine Helpers
***************************************************************************** */

/* layer 1 helpers */
#include "iodine_arg_helper.h"
#include "iodine_store.h"
/* layer 2 helpers */
#include "iodine_caller.h"
#include "iodine_json.h"
#include "iodine_pubsub_msg.h"
/* layer 1 modules */
#include "iodine_cli.h"
#include "iodine_connection.h"
#include "iodine_defer.h"
#include "iodine_mustache.h"
#include "iodine_threads.h"
#include "iodine_utils.h"
/* layer 2 modules */

#endif /* H___IODINE___H */
