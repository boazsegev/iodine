#ifndef H_IODINE_H
#define H_IODINE_H

#include "ruby.h"

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

#define IODINE_RSTRINFO(rstr)                                                  \
  ((fio_str_info_s){.len = RSTRING_LEN(rstr), .data = RSTRING_PTR(rstr)})

#endif
