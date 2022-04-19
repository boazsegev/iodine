#ifndef H_IODINE_TLS_H
#define H_IODINE_TLS_H

#include "iodine.h"

#include "fio_tls.h"

void iodine_init_tls(void);
fio_tls_s *iodine_tls2c(VALUE self);

extern VALUE IodineTLSClass;

#endif
