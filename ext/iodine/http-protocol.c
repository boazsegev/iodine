#define _GNU_SOURCE
#include "http-protocol.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

/////////////////
// functions used by the Http protocol, internally

#define _http_(protocol) ((struct HttpProtocol*)(protocol))

// implement on_close to close the FILE * for the body (if exists).
static void http_on_close(struct Server* server, int sockfd) {
  HttpRequest.destroy(Server.set_udata(server, sockfd, NULL));
}
// implement on_data to parse incoming requests.
static void http_on_data(struct Server* server, int sockfd) {
  // setup static error codes
  static char* bad_req =
      "HTTP/1.1 400 Bad HttpRequest\r\n"
      "Connection: closed\r\n"
      "Content-Length: 16\r\n\r\n"
      "Bad Http Request\r\n";
  static char* too_big_err =
      "HTTP/1.1 413 Entity Too Large\r\n"
      "Connection: closed\r\n"
      "Content-Length: 18\r\n\r\n"
      "Entity Too Large\r\n";
  static char* intr_err =
      "HTTP/1.1 502 Internal Error\r\n"
      "Connection: closed\r\n"
      "Content-Length: 16\r\n\r\n"
      "Internal Error\r\n";
  int len = 0;
  char* tmp1 = NULL;
  char* tmp2 = NULL;
  struct HttpProtocol* protocol =
      (struct HttpProtocol*)Server.get_protocol(server, sockfd);
  struct HttpRequest* request = Server.get_udata(server, sockfd);
  if (!request) {
    Server.set_udata(server, sockfd,
                     (request = HttpRequest.new(server, sockfd)));
  }
  char* buff = request->buffer;
  int pos = request->private.pos;

restart:

  // is this an ongoing request?
  if (request->body_file) {
    char buff[HTTP_HEAD_MAX_SIZE];
    int t = 0;
    while ((len = Server.read(sockfd, buff, HTTP_HEAD_MAX_SIZE)) > 0) {
      if ((t = fwrite(buff, 1, len, request->body_file)) < len) {
        perror("Tmpfile Err");
        goto internal_error;
      }
      request->private.bd_rcved += len;
    }
    if (request->private.bd_rcved >= request->content_length) {
      rewind(request->body_file);
      goto finish;
    }
    return;
  }

  // review used size
  if (HTTP_HEAD_MAX_SIZE == pos)
    goto too_big;

  // read from the buffer
  len = Server.read(sockfd, buff + pos, HTTP_HEAD_MAX_SIZE - pos);
  if (len == 0) {
    request->private.pos = pos;
    return;
  }  // buffer is empty, but more data is underway
  else if (len < 0)
    goto cleanup;  // file error

  // adjust length for buffer size positioing (so that len == max pos - 1).
  len += pos;

  // check if the request is new
  if (!pos) {
    // start parsing the request
    request->method = request->buffer;
    // get query
    while (pos < (len - 2) && buff[pos] != ' ')
      pos++;
    buff[pos++] = 0;
    if (pos > len - 3)
      goto bad_request;
    request->path = &buff[pos];
    // get query and version
    while (pos < (len - 2) && buff[pos] != ' ' && buff[pos] != '?')
      pos++;
    if (buff[pos] == '?') {
      buff[pos++] = 0;
      request->query = buff + pos;
      while (pos < (len - 2) && buff[pos] != ' ')
        pos++;
    }
    buff[pos++] = 0;
    if (pos + 5 > len)
      goto bad_request;
    request->version = &buff[pos];
    if (buff[pos] != 'H' || buff[pos + 1] != 'T' || buff[pos + 2] != 'T' ||
        buff[pos + 3] != 'P')
      goto bad_request;
    // find first header name
    while (pos < len - 2 && buff[pos] != '\r')
      pos++;
    if (pos > len - 2)  // must have 2 EOL markers before a header
      goto bad_request;
    buff[pos++] = 0;
    buff[pos++] = 0;
    request->private.header_hash = &buff[pos];
    request->private.max = pos;
  }
  while (pos < len && buff[pos] != '\r') {
    tmp1 = &buff[pos];
    while (pos + 2 < len && buff[pos] != ':') {
      if (buff[pos] >= 'a' && buff[pos] <= 'z')
        buff[pos] = buff[pos] & 223;  // uppercase the header field.
      // buff[pos] = buff[pos] | 32;    // lowercase is nice, but less common.
      pos++;
    }
    if (pos + 4 > len)  // must have at least 4 eol markers...
      goto bad_request;
    buff[pos++] = 0;
    if (buff[pos] == ' ')  // space after colon?
      buff[pos++] = 0;
    tmp2 = &buff[pos];
    // skip value
    while (pos + 2 < len && buff[pos] != '\r')
      pos++;
    if (pos + 2 > len)  // must have atleast 4 eol markers...
      goto bad_request;
    buff[pos++] = 0;
    buff[pos++] = 0;
    if (!strcmp(tmp1, "HOST")) {
      request->host = tmp2;
    } else if (!strcmp(tmp1, "CONTENT-TYPE")) {
      request->content_type = tmp2;
    } else if (!strcmp(tmp1, "CONTENT-LENGTH")) {
      request->content_length = atoi(tmp2);
    }
  }
  // check if the the request was fully sent
  if (pos >= len - 1) {
    // break it up...
    goto restart;
  }
  // set the safety endpoint
  request->private.max = pos - request->private.max;

  // check for required `host` header and body content length (not chuncked)
  if (!request->host ||
      (request->content_type &&
       !request->content_length))  // convert dynamically to Mb?
    goto bad_request;
  buff[pos++] = 0;
  buff[pos++] = 0;

  // no body, finish up
  if (!request->content_length)
    goto finish;

  // manage body
  if (request->content_length > protocol->maximum_body_size * 1024 * 1024)
    goto too_big;
  // did the body fit inside the received buffer?
  if (request->content_length + pos <= len) {
    // point the budy to the data
    request->body_str = buff + pos;
    // setup a NULL terminator?
    request->body_str[request->content_length] = 0;
    // finish up
    goto finish;
  } else {
    // we need a temporary file for the data.
    request->body_file = tmpfile();
    if (!request->body_file)
      goto internal_error;
    // write any trailing data to the tmpfile
    if (len - pos > 0) {
      if (fwrite(buff + pos, 1, len - pos, request->body_file) < len - pos)
        goto internal_error;
    }
    // add data count to marker
    request->private.bd_rcved = len - pos;
    // notifications are edge based. If there's still data in the stream, we
    // need to read it.
    goto restart;
  }

finish:

  // reset inner "pos"
  request->private.pos = 0;
  // disconnect the request object from the server storage
  // this prevents on_close from clearing the memory while on_request is still
  // accessing the request.
  Server.set_udata(server, sockfd, NULL);
  // callback
  if (protocol->on_request)
    protocol->on_request(request);
  else
    HttpRequest.destroy(request);
  // no need for cleanup
  return;

bad_request:
  // send a bed request response. hang up.
  send(sockfd, bad_req, strlen(bad_req), 0);
  close(sockfd);
  goto cleanup;

too_big:
  // send a bed request response. hang up.
  send(sockfd, too_big_err, strlen(too_big_err), 0);
  close(sockfd);
  goto cleanup;

internal_error:
  // send an internal error response. hang up.
  send(sockfd, intr_err, strlen(intr_err), 0);
  close(sockfd);
  goto cleanup;

cleanup:
  // printf("cleanup\n");
  // cleanup
  HttpRequest.destroy(Server.set_udata(server, sockfd, NULL));
  return;

  // test:
  //   static char *http_format = "HTTP/1.1 200 OK\r\n"
  //                              "Connection: keep-alive\r\n"
  //                              "Keep-Alive: 1\r\n"
  //                              "Content-Length: %d\r\n\r\n"
  //                              "%s";
  //   char *reply;
  //   // format a reply an get a pointer.
  //   asprintf(&reply, http_format, strlen("Hello"), "Hello");
  //   // send the reply
  //   protocol->write(sockfd, reply, strlen(reply));
  //   // free the pointer
  //   free(reply);
  //   return;
}

// implement on_data to parse incoming requests.
void http_default_on_request(struct HttpRequest* req) {
  // the response format
  static char* http_format =
      "HTTP/1.1 200 OK\r\n"
      "Connection: keep-alive\r\n"
      "Keep-Alive: 1\r\n"
      "Content-Length: %d\r\n\r\n"
      "%s";
  static char* http_file_echo =
      "HTTP/1.1 200 OK\r\n"
      "Connection: keep-alive\r\n"
      "Keep-Alive: 1\r\n"
      "Content-Type: %s\r\n"
      "Content-Length: %d\r\n\r\n";

  if (req->body_file) {
    char* head = NULL;
    asprintf(&head, http_file_echo, req->content_type, req->content_length);
    if (!head) {
      perror("WTF?! head");
      goto cleanup;
    }
    Server.write_move(req->server, req->sockfd, head, strlen(head));
    head = malloc(req->content_length + 1);  // the +1 is redundent.
    if (!head) {
      perror("WTF?! body");
      goto cleanup;
    }
    if (!fread(head, 1, req->content_length, req->body_file)) {
      perror("WTF?! file reading");
      free(head);
      goto cleanup;
    }
    Server.write_move(req->server, req->sockfd, head, req->content_length);
    goto cleanup;
  }
  // write reques's head onto the buffer
  char buff[HTTP_HEAD_MAX_SIZE] = {0};
  int pos = 0;
  strcpy(buff, req->method);
  pos += strlen(req->method);
  buff[pos++] = ' ';
  strcpy(buff + pos, req->path);
  pos += strlen(req->path);
  if (req->query) {
    buff[pos++] = '?';
    strcpy(buff + pos, req->query);
    pos += strlen(req->query);
  }
  buff[pos++] = ' ';
  strcpy(buff + pos, req->version);
  pos += strlen(req->version);
  buff[pos++] = '\r';
  buff[pos++] = '\n';
  HttpRequest.first(req);
  do {
    strcpy(buff + pos, HttpRequest.name(req));
    pos += strlen(HttpRequest.name(req));
    buff[pos++] = ':';
    strcpy(buff + pos, HttpRequest.value(req));
    pos += strlen(HttpRequest.value(req));
    buff[pos++] = '\r';
    buff[pos++] = '\n';
  } while (HttpRequest.next(req));

  if (req->body_str) {
    buff[pos++] = '\r';
    buff[pos++] = '\n';
    memcpy(buff + pos, req->body_str, req->content_length);
    pos += req->content_length;
  }
  buff[pos++] = 0;
  // Prep reply
  char* reply;
  int buff_len = strlen(buff);
  buff_len = asprintf(&reply, http_format, buff_len, buff);
  // check
  if (!reply) {
    perror("WTF?!");
    close(req->sockfd);
    goto cleanup;
  }
  // send(req->sockfd, reply, strlen(reply), 0);
  Server.write_move(req->server, req->sockfd, reply, buff_len);
cleanup:
  HttpRequest.destroy(req);
}

////////////////
// public API

/// returns a stack allocated, core-initialized, Http Protocol object.
struct HttpProtocol HttpProtocol(void) {
  return (struct HttpProtocol){
      .parent.service = "http",
      .parent.on_data = http_on_data,
      .parent.on_close = http_on_close,
      .maximum_body_size = 32,
      .on_request = http_default_on_request,
  };
}
