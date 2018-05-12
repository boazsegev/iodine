#ifndef H_IODINE_RAW_TCP_IP_H
#define H_IODINE_RAW_TCP_IP_H

#include "ruby.h"

void iodine_init_tcp_connections(void);
void iodine_tcp_attch_uuid(intptr_t uuid, VALUE handler);

#endif
