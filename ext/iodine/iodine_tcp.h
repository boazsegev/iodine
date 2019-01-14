#ifndef H_IODINE_RAW_TCP_IP_H
#define H_IODINE_RAW_TCP_IP_H

#include "ruby.h"

#include "iodine.h"

void iodine_init_tcp_connections(void);
void iodine_tcp_attch_uuid(intptr_t uuid, VALUE handler);
intptr_t iodine_tcp_listen(iodine_connection_args_s args);
intptr_t iodine_tcp_connect(iodine_connection_args_s args);

#endif
