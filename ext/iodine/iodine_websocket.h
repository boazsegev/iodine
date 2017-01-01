/*
Copyright: Boaz segev, 2016-2017
License: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef IODINE_WEBSOCKETS_H
#define IODINE_WEBSOCKETS_H

#include <ruby.h>
#include "websockets.h"

void Init_iodine_websocket(void);

void iodine_websocket_upgrade(http_request_s *request,
                              http_response_s *response, VALUE handler);

extern size_t iodine_websocket_max_msg_size;
extern uint8_t iodine_websocket_timeout;

#endif /* IODINE_WEBSOCKETS_H */
