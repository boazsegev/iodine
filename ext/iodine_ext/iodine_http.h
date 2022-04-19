#ifndef H_IODINE_HTTP_H
#define H_IODINE_HTTP_H
/*
Copyright: Boaz segev, 2016-2018
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine.h"

/* these three are used also by rb-rack-io.c */
extern VALUE IODINE_R_INPUT;
extern VALUE IODINE_R_INPUT_DEFAULT;
extern VALUE IODINE_R_HIJACK;
extern VALUE IODINE_R_HIJACK_IO;
extern VALUE IODINE_R_HIJACK_CB;
void iodine_init_http(void);

intptr_t iodine_http_listen(iodine_connection_args_s args);
// intptr_t iodine_http_connect(iodine_connection_args_s args); // not yet...
intptr_t iodine_ws_connect(iodine_connection_args_s args);

#endif
