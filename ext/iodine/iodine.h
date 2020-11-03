#ifndef H_IODINE_H
#define H_IODINE_H

#include "ruby.h"

#include "fio.h"
#include "fio_tls.h"
#include "fiobj.h"
/* used for iodine_connect and iodine_listen routing */
typedef struct {
  fio_str_info_s address;
  fio_str_info_s port;
  fio_str_info_s method;
  fio_str_info_s path;
  fio_str_info_s body;
  fio_str_info_s public;
  fio_str_info_s url;
  fio_tls_s *tls;
  VALUE handler;
  FIOBJ headers;
  FIOBJ cookies;
  size_t max_headers;
  size_t max_body;
  intptr_t max_clients;
  size_t max_msg;
  uint8_t timeout;
  uint8_t ping;
  uint8_t log;
  enum {
    IODINE_SERVICE_RAW,
    IODINE_SERVICE_HTTP,
    IODINE_SERVICE_WS,
  } service;
} iodine_connection_args_s;

#include "iodine_caller.h"
#include "iodine_connection.h"
#include "iodine_defer.h"
#include "iodine_helpers.h"
#include "iodine_http.h"
#include "iodine_json.h"
#include "iodine_mustache.h"
#include "iodine_pubsub.h"
#include "iodine_rack_io.h"
#include "iodine_store.h"
#include "iodine_tcp.h"
#include "iodine_tls.h"

/* *****************************************************************************
Constants
***************************************************************************** */
extern VALUE IodineModule;
extern VALUE IodineBaseModule;
extern VALUE iodine_default_args;
extern ID iodine_call_id;
extern ID iodine_to_s_id;

#define IODINE_RSTRINFO(rstr)                                                  \
  ((fio_str_info_s){.len = RSTRING_LEN(rstr), .data = RSTRING_PTR(rstr)})

#endif
