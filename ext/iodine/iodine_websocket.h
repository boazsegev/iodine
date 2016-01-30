/*
copyright: Boaz segev, 2015
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef IODINE_WEBSOCKETS_H
#define IODINE_WEBSOCKETS_H
#include "iodine.h"
#include "http-request.h"
#include "http-status.h"
#include "iodine_http.h"

extern struct __Websockets__CLASS__ {
  void (*init)(void);
  void (*new)(struct HttpRequest* request, VALUE handler);
  void (*reject)(struct HttpRequest* request);
} Websockets;

#endif /* HTTP_H */
