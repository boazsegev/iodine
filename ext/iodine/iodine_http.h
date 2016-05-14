/*
copyright: Boaz segev, 2015
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef HTTP_H
#define HTTP_H
#include "iodine.h"
#include "websockets.h"
#include "rb-rack-io.h"

void Init_iodine_http(void);
extern VALUE rHttp;

#endif /* HTTP_H */
