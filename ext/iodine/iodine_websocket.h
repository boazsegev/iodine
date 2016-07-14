/*
copyright: Boaz segev, 2016
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef IODINE_WEBSOCKETS_H
#define IODINE_WEBSOCKETS_H

#include <ruby.h>
#include "websockets.h"

void Init_iodine_websocket(void);

void iodine_websocket_upgrade(http_request_s* request,
                              http_response_s* response,
                              VALUE handler);

#endif /* IODINE_WEBSOCKETS_H */
