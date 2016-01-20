/*
copyright: Boaz segev, 2015
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef HTTP_H
#define HTTP_H
#include "lib-server.h"
#include "http-protocol.h"
#include "http-request.h"
#include <stdio.h>

// This file defines helpers and settings that allow using "lib-server" as an
// HTTP server platform.
//
// So far, only Http/1.x is supported. Future support for Http/2 and Websockets
// is planned. SSL is, at the moment missing. It will be provided as a wrapper
// library later on, using OpenSSL.
//
extern const struct HttpClass {
  // returns a new Http/1.x protocol object, for you to set the particulars.
  struct HttpProtocol (*http1p)(void);
  // wraps an HttpProtocol struct within a ServerSettings object, setting up
  // default values.
  struct ServerSettings (*http_server)(struct HttpProtocol* protocol);
} Http;

#endif /* HTTP_H */
