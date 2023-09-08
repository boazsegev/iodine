#ifndef H___IODINE___H
#define H___IODINE___H
#include <ruby.h>
#include <ruby/encoding.h>
#include <ruby/intern.h>
#include <ruby/thread.h>

typedef int fio_thread_pid_t;
typedef VALUE fio_thread_t;

static VALUE iodine_rb_IODINE;
static rb_encoding *IodineUTF8Encoding;

#define FIO_LEAK_COUNTER            1
#define FIO_MUSTACHE_LAMBDA_SUPPORT 1
#define FIO_THREADS_BYO             1
#define FIO_THREADS_FORK_BYO        1
#define FIO_EVERYTHING
#include "fio-stl.h"
// #define FIO_FIOBJ
// #include "fio-stl.h"

#include "iodine_arg_helper.h"
#include "iodine_caller.h"
#include "iodine_store.h"
#include "iodine_threads.h"

#include "iodine_json.h"
#endif /* H___IODINE___H */
