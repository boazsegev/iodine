/*
copyright: Boaz segev, 2015
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
#ifndef WEBSOCKET_H
#define WEBSOCKET_H

// Designed for the lib-server library.
#include "lib-server.h"

// The Websocket protocol object will expect to have a handler that handles any
// websocket events. This is the struct for the handler.
struct WSHandler {
  // called once a websocket connection was established.
  void (*on_open)(server_pt srv, int sockfd);
  // called whenever a websocket messsage was received.
  void (*on_message)(server_pt srv, int sockfd, void* data, long length);
  // called when the server is shutting down and before the socket is closed.
  void (*on_shutdown)(server_pt srv, int sockfd);
  // called once a connection was closed.
  void (*on_close)(server_pt srv, int sockfd);
};

/////////
// Use the Websocket Protocol as a Protocol object for websocket connections.
// (simpley cast the WSProtocol pointer to a Protocol pointer)
struct WSProtocol;

/////////
// The API's gateway (namespace)
extern struct ___Websockets_API__ {
  struct WSProtocol (*new)(struct WSHandler* handler);
  // sends text data through the websocket, wrapping it correctly as a protocol
  // message.
  //
  // returns 0 on success and -1 on error.
  int (*send_text)(server_pt srv, int sockfd, void* data, size_t length);
  // sends binary data through the websocket, wrapping it correctly as a
  // protocol message.
  //
  // returns 0 on success and -1 on error.
  int (*send_binary)(server_pt srv, int sockfd, void* data, size_t length);
  // gracefully closes a websocket connection.
  void (*close)(server_pt srv, int sockfd);
  // sets the default timeout for new websocket connections. defaults to 45.
  void (*set_timeout)(unsigned char);
  // sets the maximum size for a message's body. Defaults to 65,536 bytes.
  void (*set_max_body_size)(size_t max_body_size);
} Websockets;
#endif /* WEBSOCKET_H */
