#include "http.h"

static struct ServerSettings http_server(struct HttpProtocol* protocol) {
  return (struct ServerSettings){
      .timeout = 1, .protocol = (struct Protocol*)protocol,
  };
}

const struct HttpClass Http = {.http1p = HttpProtocol,
                               .http_server = http_server};
