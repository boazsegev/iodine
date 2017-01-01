/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef IODINE_HTTP_H
#define IODINE_HTTP_H
#include "iodine_core.h"
#include "rb-rack-io.h"

/* the Iodine::Rack HTTP server class*/
extern VALUE IodineHttp;
/* these three are used also by rb-rack-io.c */
extern VALUE R_HIJACK;
extern VALUE R_HIJACK_IO;
extern VALUE R_HIJACK_CB;

/* Initializes the HTTP library */
void Init_iodine_http(void);

/* Reviews the HTTP settngs and initiates an HTTP service if required */
int iodine_http_review(void);

#endif
