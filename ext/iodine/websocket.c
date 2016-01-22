/*
copyright: Boaz segev, 2015
license: MIT

Feel free to copy, use and enjoy according to the license provided.
*/
// Designed for the lib-server library.
#include "websocket.h"

// some defaults for the protocol
static size_t websocket_max_body_size = 65536;
static unsigned char websocket_def_timeout = 45;

/////////////////////////////////
// Parsing types
struct WSHead {
  unsigned fin : 1;
  unsigned rsv1 : 1;
  unsigned rsv2 : 1;
  unsigned rsv3 : 1;
  unsigned op_code : 4;
  unsigned masked : 1;
  unsigned size : 7;
};
union WSSize {
  unsigned len1 : 16;
  unsigned long len2 : 64;
};

// The parser's data
struct WSParser {
  void* buffer;
  size_t buffer_size;
  size_t length;
  size_t received;
  struct WSHead last_head;
  char mask[4];
};

/////////
// Use the Websocket Protocol as a Protocol object for websocket connections.
struct WSProtocol {
  struct Protocol protocol;
  struct WSHandler* handler;
  struct WSParser parser;
};

/////////
// Use the Websocket Protocol callbacks.

static void on_open(server_pt srv, int sockfd) {}

static void ping(server_pt srv, int sockfd) {
  // send a ping using: (will close the connection if `send` fails)
  Server.write_urgent(srv, sockfd, "\x89\x00", 2);
}

static void on_data(server_pt srv, int sockfd) {
  // parse
  // if there's a message:
  // // handle protocol messages.
  // // forward user messages
  // // store state(?) return.
}

/////////
// The API functions.
static struct WSProtocol new_protocol(struct WSHandler* handler) {
  return (struct WSProtocol){
      .protocol.on_open = on_open,
      .protocol.ping = ping,
      .protocol.on_data = on_data,
      .protocol.on_close = handler->on_close,
      .protocol.on_shutdown = handler->on_shutdown,
      .handler = handler,
  };
}

// sets the default timeout for new websocket connections. defaults to 45.
static void set_timeout(unsigned char def_timeout) {
  websocket_def_timeout = def_timeout;
}

// sets the maximum size for a message's body.
static void set_max_body_size(size_t max_bsize) {
  websocket_max_body_size = max_bsize;
}

int ws_send_text(server_pt srv, int sockfd, void* data, size_t length);

int ws_send_binary(server_pt srv, int sockfd, void* data, size_t length);

void ws_close(server_pt srv, int sockfd);

/////////
// The API's gateway (namespace)
struct ___Websockets_API__ Websockets = {
    .new = new_protocol,
    .close = ws_close,
    .send_text = ws_send_text,
    .send_binary = ws_send_binary,
    .set_max_body_size = set_max_body_size,
    .set_timeout = set_timeout,
};
