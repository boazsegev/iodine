#ifndef H_IODINE_WEBSOCKETS_H
#define H_IODINE_WEBSOCKETS_H
/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#include "iodine.h"

#include "http.h"

void Iodine_init_websocket(void);

void iodine_websocket_upgrade(http_s *request, VALUE handler);

#endif
