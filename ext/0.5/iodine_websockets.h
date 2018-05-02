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

void iodine_upgrade_websocket(http_s *request, VALUE handler);
void iodine_upgrade_sse(http_s *h, VALUE handler);
#endif
