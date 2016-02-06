#include "iodine_websocket.h"
#include "iodine_http.h"
#include <ruby/io.h>
// includes SHA1 functions locally, as static functions
#include "sha1.inc"

/* ////////////////////////////////////////////////////////////
This file creates an HTTP server based on the Iodine libraries.

The server is (mostly) Rack compatible, except:

1. upgrade requests are handled using special upgrade handlers.
2. if a String is returned, it is a assumed to be a status 200 Html data?

//////////////////////////////////////////////////////////// */

// This one is shared
VALUE rHttp;  // The Iodine::Http class

//////////////
// general global definitions we will use herein.
static ID server_var_id;    // id for the Server variable (pointer)
static ID fd_var_id;        // id for the file descriptor (Fixnum)
static ID call_proc_id;     // id for `#call`
static ID each_method_id;   // id for `#call`
static ID to_s_method_id;   // id for `#call`
static ID new_func_id;      // id for the Class.new method
static ID on_open_func_id;  // the on_open callback's ID
static VALUE _hijack_sym;

// for Rack
static VALUE REQUEST_METHOD;      // for Rack
static VALUE CONTENT_TYPE;        // for Rack.
static VALUE CONTENT_LENGTH;      // for Rack.
static VALUE SCRIPT_NAME;         // for Rack
static VALUE PATH_INFO;           // for Rack
static VALUE QUERY_STRING;        // for Rack
static VALUE QUERY_ESTRING;       // for rack (if no query)
static VALUE SERVER_NAME;         // for Rack
static VALUE SERVER_PORT;         // for Rack
static VALUE SERVER_PORT_80;      // for Rack
static VALUE SERVER_PORT_443;     // for Rack
static VALUE R_VERSION;           // for Rack: rack.version
static VALUE R_VERSION_V;         // for Rack: rack.version
static VALUE R_SCHEME;            // for Rack: rack.url_scheme
static VALUE R_SCHEME_HTTP;       // for Rack: rack.url_scheme value
static VALUE R_SCHEME_HTTPS;      // for Rack: rack.url_scheme value
static VALUE R_INPUT;             // for Rack: rack.input
static VALUE R_ERRORS;            // for Rack: rack.errors
static VALUE R_ERRORS_V;          // for Rack: rack.errors
static VALUE R_MTHREAD;           // for Rack: rack.multithread
static VALUE R_MTHREAD_V;         // for Rack: rack.multithread
static VALUE R_MPROCESS;          // for Rack: rack.multiprocess
static VALUE R_MPROCESS_V;        // for Rack: rack.multiprocess
static VALUE R_RUN_ONCE;          // for Rack: rack.run_once
static VALUE R_HIJACK_Q;          // for Rack: rack.hijack?
VALUE R_HIJACK;                   // for Rack: rack.hijack
static VALUE R_HIJACK_V;          // for Rack: rack.hijack
VALUE R_HIJACK_IO;                // for Rack: rack.hijack_io
static VALUE R_IOFD;              // Iodine variables on Rack: iodine.fd
static VALUE XSENDFILETYPE;       // sendfile support
static VALUE XSENDFILE;           // sendfile support
static VALUE UPGRADE_HEADER;      // upgrade support
static VALUE CONNECTION_HEADER;   // upgrade support
static VALUE CONNECTION_CLOSE;    // upgrade support
static VALUE WEBSOCKET_STR;       // upgrade support
static VALUE WEBSOCKET_VER;       // upgrade support
static VALUE WEBSOCKET_SEC_VER;   // upgrade support
static VALUE WEBSOCKET_SEC_EXT;   // upgrade support
static VALUE WEBSOCKET_SEC_ACPT;  // upgrade support
// rack.version must be an array of Integers.
// rack.url_scheme must either be http or https.
// There must be a valid input stream in rack.input.
// There must be a valid error stream in rack.errors.
// There may be a valid hijack stream in rack.hijack_io
// The REQUEST_METHOD must be a valid token.
// The SCRIPT_NAME, if non-empty, must start with /
// The PATH_INFO, if non-empty, must start with /
// The CONTENT_LENGTH, if given, must consist of digits only.
// One of SCRIPT_NAME or PATH_INFO must be set. PATH_INFO should be / if
// SCRIPT_NAME is empty. SCRIPT_NAME never should be /, but instead be
// empty.

/* ////////////////////////////////////////////////////////////

The Http on_request handling functions

//////////////////////////////////////////////////////////// */

// translate a struct HttpRequest to a Hash, according top the
// Rack specifications.
static VALUE request_to_env(struct HttpRequest* request) {
  // return Qnil;
  // Create the env Hash
  VALUE env = rb_hash_new();
  // Register the object
  Registry.add(env);
  // set the simple core request data
  rb_hash_aset(
      env, REQUEST_METHOD,
      rb_enc_str_new(request->method, strlen(request->method), BinaryEncoding));
  rb_hash_aset(
      env, PATH_INFO,
      rb_enc_str_new(request->path, strlen(request->path), BinaryEncoding));
  rb_hash_aset(
      env, QUERY_STRING,
      (request->query ? rb_enc_str_new(request->query, strlen(request->query),
                                       BinaryEncoding)
                      : QUERY_ESTRING));
  rb_hash_aset(env, R_IOFD, INT2FIX(request->sockfd));

  // setup static env data
  rb_hash_aset(env, R_VERSION, R_VERSION_V);
  rb_hash_aset(env, SCRIPT_NAME, QUERY_ESTRING);
  rb_hash_aset(env, R_ERRORS, R_ERRORS_V);
  rb_hash_aset(env, R_MTHREAD, R_MTHREAD_V);
  rb_hash_aset(env, R_MPROCESS, R_MPROCESS_V);
  rb_hash_aset(env, R_RUN_ONCE, Qfalse);
  // set scheme to R_SCHEME_HTTP or R_SCHEME_HTTPS or dynamic
  int ssl = 0;
  {
    if (HttpRequest.find(request, "X-FORWARDED-PROTO")) {
      // this is the protocol
      char* proto = HttpRequest.value(request);
      if (!strcasecmp(proto, "http")) {
        rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
      } else if (!strcasecmp(proto, "https")) {
        ssl = 1;
        rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTPS);
      } else {
        rb_hash_aset(env, R_SCHEME, rb_str_new_cstr(proto));
      }
    } else if (HttpRequest.find(request, "FORWARDED")) {
      // placeholder
      char* proto = HttpRequest.value(request);
      int pos = 0;
      int len = strlen(proto);
      while (pos < len) {
        if (((proto[pos++] | 32) == 'p') && ((proto[pos++] | 32) == 'r') &&
            ((proto[pos++] | 32) == 'o') && ((proto[pos++] | 32) == 't') &&
            ((proto[pos++] | 32) == 'o') && ((proto[pos++] | 32) == '=')) {
          if (!strncasecmp(proto + pos, "http", 4)) {
            if ((proto + pos)[4] == 's') {
              ssl = 1;
              rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTPS);
            } else {
              rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
            }
          } else {
            int pos_e = pos;
            while ((pos_e < len) && (proto[pos_e] != ';'))
              pos_e++;
            rb_hash_aset(env, R_SCHEME, rb_str_new(proto + pos, pos_e - pos));
          }
        }
      }
      // default to http
      if (pos == len)
        rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
    } else
      rb_hash_aset(env, R_SCHEME, R_SCHEME_HTTP);
  }
  // set host data
  {
    int pos = 0;
    int len = strlen(request->host);
    for (; pos < len; pos++) {
      if (request->host[pos] == ':')
        break;
    }
    rb_hash_aset(env, SERVER_NAME, rb_str_new(request->host, pos));
    if (++pos < len)
      rb_hash_aset(env, SERVER_PORT,
                   rb_str_new(request->host + pos, len - pos));
    else
      rb_hash_aset(env, SERVER_PORT, (ssl ? SERVER_PORT_443 : SERVER_PORT_80));
  }
  // set POST data
  if (request->content_length) {
    // check for content type
    if (request->content_type) {
      rb_hash_aset(
          env, CONTENT_TYPE,
          rb_enc_str_new(request->content_type, strlen(request->content_type),
                         BinaryEncoding));
    }
    // CONTENT_LENGTH should be a string (damn stupid Rack specs).
    HttpRequest.find(request, "CONTENT-LENGTH");
    char* value = HttpRequest.value(request);
    rb_hash_aset(env, CONTENT_LENGTH,
                 rb_enc_str_new(value, strlen(value), BinaryEncoding));
  }
  VALUE rack_io = 0;
  rb_hash_aset(env, R_INPUT, (rack_io = RackIO.new(request, env)));
  // setup Hijacking headers
  //   rb_hash_aset(env, R_HIJACK_Q, Qfalse);
  VALUE hj_method = rb_obj_method(rack_io, _hijack_sym);
  rb_hash_aset(env, R_HIJACK_Q, Qtrue);
  rb_hash_aset(env, R_HIJACK, hj_method);
  rb_hash_aset(env, R_HIJACK_IO, Qnil);
  // itterate through the headers and set the HTTP_X "variables"
  HttpRequest.first(request);
  {
    char *name, *value;
    VALUE header;
    do {
      name = HttpRequest.name(request);
      value = HttpRequest.value(request);
      // careful, pointer comparison crashed Ruby (although it works without
      // Ruby)... this could be an issue.
      if (value == (request->content_type) ||
          (name[0] == 'C' && !strcmp(name, "CONTENT-LENGTH")))
        continue;
      header = rb_sprintf("HTTP_%s", name);
      rb_enc_associate(header, BinaryEncoding);
      rb_hash_aset(env, header,
                   rb_enc_str_new(value, strlen(value), BinaryEncoding));
    } while (HttpRequest.next(request));
  }
  HttpRequest.first(request);
  return env;
}

// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static int for_each_header_pair(VALUE key, VALUE val, VALUE _req) {
  struct HttpRequest* request = (void*)_req;
  int pos_s = 0, pos_e = 0;
  if (TYPE(key) != T_STRING)
    key = RubyCaller.call_unsafe(key, to_s_method_id);
  if (TYPE(val) != T_STRING) {
    if (TYPE(val) == T_FIXNUM) {
      request->private.pos +=
          snprintf(request->buffer + request->private.pos,
                   HTTP_HEAD_MAX_SIZE - request->private.pos, "%.*s: %ld\r\n",
                   (int)RSTRING_LEN(key), RSTRING_PTR(key), FIX2LONG(val));
      return ST_CONTINUE;
    }
    val = RubyCaller.call_unsafe(val, to_s_method_id);
    if (val == Qnil)
      return ST_STOP;
  }
  char* key_s = RSTRING_PTR(key);
  char* val_s = RSTRING_PTR(val);
  int key_len = RSTRING_LEN(key), val_len = RSTRING_LEN(val);
  // scan the value for newline (\n) delimiters
  while (pos_e < val_len) {
    // make sure we don't overflow
    if (request->private.pos >= HTTP_HEAD_MAX_SIZE)
      return ST_STOP;
    // scanning for newline (\n) delimiters
    while (pos_e < val_len && val_s[pos_e] != '\n')
      pos_e++;
    // whether we hit a delimitor or the end of string, write the header
    request->private.pos +=
        snprintf(request->buffer + request->private.pos,
                 HTTP_HEAD_MAX_SIZE - request->private.pos, "%.*s: %.*s\r\n",
                 key_len, key_s, pos_e - pos_s, val_s + pos_s);
    // move forward (skip the '\n' if exists)
    pos_s = pos_e + 1;
    pos_e++;
  }
  // no errors, return 0
  return ST_CONTINUE;
}
// itterate through the headers and add them to the response buffer
// (we are recycling the request's buffer)
static VALUE for_each_string(VALUE str, VALUE _req, int argc, VALUE argv) {
  struct HttpRequest* request = (void*)_req;
  // write body
  if (TYPE(str) != T_STRING || !RSTRING_LEN(str)) {
    return Qfalse;
  }
  Server.write(request->server, request->sockfd, RSTRING_PTR(str),
               RSTRING_LEN(str));
  return Qtrue;
}
// translate a struct HttpRequest to a Hash, according top the
// Rack specifications.
static int send_response(struct HttpRequest* request, VALUE response) {
  static char internal_error[] =
      "HTTP/1.1 502 Internal Error\r\n"
      "Connection: closed\r\n"
      "Content-Length: 16\r\n\r\n"
      "Internal Error\r\n";

  VALUE tmp;
  char* tmp_s;
  char close_when_done = 0;

  // check for keep alive
  if (HttpRequest.find(request, "CONNECTION")) {
    if ((HttpRequest.value(request)[0] | 32) == 'c')
      close_when_done = 1;
    // done with CONNECTION header
  } else if (request->version[6] != '.') {
    // no connection header, check version
    close_when_done = 1;
  }

  // nil is a bad response... we have an error
  if (response == Qnil)
    goto internal_err;
  // false is an intentional ignore - this is not an error (might be a highjack)
  if (response == Qfalse)
    goto unknown_stop;
  if (TYPE(response) != T_ARRAY)
    goto internal_err;
  if (RARRAY_LEN(response) < 3)
    goto internal_err;

  // get status code from array (obj 0)
  // NOTICE: this may not always be a number (could be the string "200").
  tmp = rb_ary_entry(response, 0);
  if (TYPE(tmp) == T_STRING)
    tmp = rb_str_to_inum(tmp, 10, 0);
  if (TYPE(tmp) != T_FIXNUM || (tmp = FIX2INT(tmp)) > 512 || tmp < 100 ||
      !(tmp_s = HttpStatus.to_s(tmp)))
    goto internal_err;
  request->private.pos = 0;
  request->private.pos +=
      snprintf(request->buffer, HTTP_HEAD_MAX_SIZE - request->private.pos,
               "HTTP/1.1 %lu %s\r\n", tmp, tmp_s);

  // Start printing headers to head-buffer
  tmp = rb_ary_entry(response, 1);
  if (TYPE(tmp) != T_HASH)
    goto internal_err;
  rb_hash_foreach(tmp, for_each_header_pair, (VALUE)request);
  // make sure we're not overflowing
  if (request->private.pos >= HTTP_HEAD_MAX_SIZE - 2) {
    rb_warn("Header overflow detected! Header size is limited to ~8Kb.");
    goto internal_err;
  }
  // review connection (+ keep alive) and date headers
  tmp = 0;
  request->private.max = 0;
  // review buffer and check if the headers exist
  while (request->private.max < request->private.pos) {
    if ((request->buffer[request->private.max++]) != '\n')
      continue;
    if ((request->buffer[request->private.max++] | 32) == 'c' &&
        (request->buffer[request->private.max++] | 32) == 'o' &&
        (request->buffer[request->private.max++] | 32) == 'n' &&
        (request->buffer[request->private.max++] | 32) == 'n' &&
        (request->buffer[request->private.max++] | 32) == 'e' &&
        (request->buffer[request->private.max++] | 32) == 'c' &&
        (request->buffer[request->private.max++] | 32) == 't' &&
        (request->buffer[request->private.max++] | 32) == 'i' &&
        (request->buffer[request->private.max++] | 32) == 'o' &&
        (request->buffer[request->private.max++] | 32) == 'n' &&
        (request->buffer[request->private.max++] | 32) == ':') {
      tmp = tmp | 2;
      // check for close twice, as the first 'c' could be a space
      if ((request->buffer[request->private.max++] | 32) == 'c' ||
          (request->buffer[request->private.max++] | 32) == 'c') {
        close_when_done = 1;
      }
    }
    // check for date. The "d" was already skipped over
    if ((request->buffer[request->private.max++] | 32) == 'a' &&
        (request->buffer[request->private.max++] | 32) == 't' &&
        (request->buffer[request->private.max++] | 32) == 'e' &&
        (request->buffer[request->private.max++] | 32) == ':')
      tmp = tmp | 4;
    // check for content length
    if (  // "con" passed "t" failed (connect), "e" failed (date), check the
        // rest
        (request->buffer[request->private.max++] | 32) == 'n' &&
        (request->buffer[request->private.max++] | 32) == 't' &&
        (request->buffer[request->private.max++] | 32) == '-') {
      if ((request->buffer[request->private.max++] | 32) == 'l' &&
          (request->buffer[request->private.max++] | 32) == 'e' &&
          (request->buffer[request->private.max++] | 32) == 'n' &&
          (request->buffer[request->private.max++] | 32) == 'g' &&
          (request->buffer[request->private.max++] | 32) == 't' &&
          (request->buffer[request->private.max++] | 32) == 'h' &&
          (request->buffer[request->private.max++] | 32) == ':')
        tmp = tmp | 8;
      // check for "content-encoding"
      if (  // "content-e" passed on failing "content-length" (__ncoding)
          (request->buffer[request->private.max++] | 32) == 'n' &&
          (request->buffer[request->private.max++] | 32) == 'c' &&
          (request->buffer[request->private.max++] | 32) == 'o' &&
          (request->buffer[request->private.max++] | 32) == 'd' &&
          (request->buffer[request->private.max++] | 32) == 'i' &&
          (request->buffer[request->private.max++] | 32) == 'n' &&
          (request->buffer[request->private.max++] | 32) == 'g' &&
          (request->buffer[request->private.max++] | 32) == ':')
        tmp = tmp | 16;
    }
  }
  // if we don't have a content length we need to close the connection when
  // done.
  if (!(tmp & 8) && !(tmp & 2))
    close_when_done = 1;
  // if the connection headers aren't there, add them
  if (!(tmp & 2)) {
    if (close_when_done) {
      static char close_conn_header[] = "Connection: close\r\n";
      if (request->private.pos + sizeof(close_conn_header) >=
          HTTP_HEAD_MAX_SIZE - 2) {
        rb_warn("Header overflow detected! Header size is limited to ~8Kb.");
        goto internal_err;
      }
      memcpy(request->buffer + request->private.pos, close_conn_header,
             sizeof(close_conn_header));
      request->private.pos += sizeof(close_conn_header) - 1;
    } else {
      static char kpalv_conn_header[] =
          "Connection: keep-alive\r\nKeep-Alive: timeout=2\r\n";
      if (request->private.pos + sizeof(kpalv_conn_header) >=
          HTTP_HEAD_MAX_SIZE - 2) {
        rb_warn("Header overflow detected! Header size is limited to ~8Kb.");
        goto internal_err;
      }
      memcpy(request->buffer + request->private.pos, kpalv_conn_header,
             sizeof(kpalv_conn_header));
      request->private.pos += sizeof(kpalv_conn_header) - 1;
    }
  }
  if (!(tmp & 4)) {
    struct tm t;
    gmtime_r(&Server.reactor(request->server)->last_tick, &t);
    request->private.pos +=
        strftime(request->buffer + request->private.pos,
                 HTTP_HEAD_MAX_SIZE - request->private.pos - 2,
                 "Date: %a, %d %b %Y %H:%M:%S GMT\r\n", &t);
  }
  // write the extra EOL markers
  request->buffer[request->private.pos++] = '\r';
  request->buffer[request->private.pos++] = '\n';

  // write headers to server
  Server.write(request->server, request->sockfd, request->buffer,
               request->private.pos);

  // write body
  tmp = rb_ary_entry(response, 2);
  if (TYPE(tmp) == T_ARRAY) {
    // [String] is most likely
    int len = RARRAY_LEN(tmp);
    VALUE str;
    for (size_t i = 0; i < len; i++) {
      str = rb_ary_entry(tmp, i);
      if (TYPE(str) != T_STRING) {
        fprintf(stderr, "data in array isn't a string! (index %lu)\n", i);
        goto internal_err;
      }
      if (RSTRING_PTR(str) && RSTRING_LEN(str) &&
          !Server.write(request->server, request->sockfd, RSTRING_PTR(str),
                        RSTRING_LEN(str)))
        goto unknown_stop;
    }
  } else if (TYPE(tmp) == T_STRING) {
    // String is a likely error
    if (RSTRING_LEN(tmp))
      Server.write(request->server, request->sockfd, RSTRING_PTR(tmp),
                   RSTRING_LEN(tmp));
  } else if (tmp == Qnil) {
    // nothing to do.
    // This could be a websocket/upgrade decision.
  } else {
    rb_block_call(tmp, each_method_id, 0, NULL, for_each_string,
                  (VALUE)request);
  }

  if (close_when_done)
    Server.close(request->server, request->sockfd);

unknown_stop:
  return close_when_done;
internal_err:
  // Registry.remove(response);
  Server.write(request->server, request->sockfd, internal_error,
               sizeof(internal_error));
  Server.close(request->server, request->sockfd);
  rb_warn(
      "Invalid HTTP response, send 502 error code and closed connectiom.\n"
      "The response must be an Array:\n[<Fixnum - status code>, "
      "{<Hash-headers>}, [<Array/IO - data], <optional Protocol "
      "for Upgrade>]");
  return 1;
}
// Upgrade to a generic Protocol (Iodine Core style)
static void perform_generic_upgrade(struct HttpRequest* request,
                                    VALUE response) {
  VALUE tmp = 0;
  // Upgrade (if 4th element)
  if (RARRAY_LEN(response) > 3) {
    tmp = rb_ary_entry(response, 3);
    // no real upgrade element
    if (tmp == Qnil)
      return;
    // include the rDynProtocol within the object.
    if (TYPE(tmp) == T_CLASS) {
      // include the Protocol module
      // // do we neet to check?
      // if (rb_mod_include_p(protocol, rDynProtocol) == Qfalse)
      rb_include_module(tmp, rDynProtocol);
      tmp = RubyCaller.call_unsafe(tmp, new_func_id);
    } else {
      // include the Protocol module in the object's class
      VALUE p_class = rb_obj_class(tmp);
      // // do we neet to check?
      // if (rb_mod_include_p(p_class, rDynProtocol) == Qfalse)
      rb_include_module(p_class, rDynProtocol);
    }
    // make sure everything went as it should
    if (tmp == Qnil)
      return;
    // set the new protocol at the server's udata
    Server.set_udata(request->server, request->sockfd, (void*)tmp);
    // add new protocol to the Registry
    Registry.add(tmp);
    // initialize pre-required variables
    rb_ivar_set(tmp, fd_var_id, INT2FIX(request->sockfd));
    set_server(tmp, request->server);
    // initialize the new protocol
    RubyCaller.call_unsafe(tmp, on_open_func_id);
  }
}
// Gets the response object, within a GVL context
static void* handle_request_in_gvl(void* _res) {
  struct HttpRequest* request = _res;
  VALUE env = request_to_env(request);
  VALUE response;
  char* recv_str = NULL;
  static char ws_key_accpt_str[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  // check for a valid and supported Websocket request (ver 13)
  // If exists, forward this request to the special handler
  // Notice the assignment of the callback stored as udata 1.
  if (request->upgrade && !strcasecmp(request->upgrade, "websocket") &&
      (response = (VALUE)Server.get_udata(request->server, 1)) &&
      HttpRequest.find(request, "SEC-WEBSOCKET-VERSION") &&
      (recv_str = HttpRequest.value(request)) && recv_str[0] == '1' &&
      recv_str[1] == '3') {
    // a regular request is forwarded to the websocket callback (stored in 0).
    response = RubyCaller.call_unsafe2(response, call_proc_id, 1, &env);
    // clean-up env and register response
    if (Registry.replace(env, response))
      Registry.add(response);
    // update response for Websocket support
    if (TYPE(response) == T_ARRAY && RARRAY_LEN(response) > 3) {
      // upgrade taking place, make sure the upgrade headers are valid for the
      // response.
      rb_ary_store(response, 0, INT2FIX(101));  // status
      rb_ary_store(response, 2, Qnil);          // no content.
      // we're done with the `env` variable, so we can use it to store the
      // headers.
      env = rb_ary_entry(response, 1);
      // set content-length to 0 - needed?
      // rb_hash_aset(env, CONTENT_LENGTH, INT2FIX(0));
      // connection and upgrade headers
      rb_hash_aset(env, CONNECTION_HEADER, UPGRADE_HEADER);
      rb_hash_aset(env, UPGRADE_HEADER, WEBSOCKET_STR);
      // websocket version (13)
      rb_hash_aset(env, WEBSOCKET_SEC_VER, WEBSOCKET_VER);
      // websocket extentions (none)
      // rb_hash_aset(env, WEBSOCKET_SEC_EXT, QUERY_ESTRING);
      // the accept Base64 Hash - we need to compute this one and set it
      if (!HttpRequest.find(request, "SEC-WEBSOCKET-KEY"))
        goto refuse_websocket;
      // the client's unique string
      recv_str = HttpRequest.value(request);
      if (!recv_str)
        goto refuse_websocket;
      ;
      // use the SHA1 methods provided to concat the client string and hash
      struct sha1nfo sha1;
      sha1_init(&sha1);
      sha1_write(&sha1, recv_str, strlen(recv_str));
      sha1_write(&sha1, ws_key_accpt_str, sizeof(ws_key_accpt_str) - 1);
      // create a ruby stribg to contain the data
      VALUE accpt_str = rb_str_buf_new(30);
      if (accpt_str == Qnil)
        goto refuse_websocket;
      // base encode the data
      int len = ws_base64_encode((char*)sha1_result(&sha1),
                                 RSTRING_PTR(accpt_str), 20);
      // set the string's length and encoding
      rb_str_set_len(accpt_str, len);
      rb_enc_associate_index(accpt_str, BinaryEncodingIndex);
      // set the accept hashed value in the headers
      rb_hash_aset(env, WEBSOCKET_SEC_ACPT, accpt_str);
    } else if (TYPE(response) == T_ARRAY && RARRAY_LEN(response) > 2) {
    refuse_websocket:
      // no upgrade object - send 400 error with headers.
      rb_ary_store(response, 0, INT2FIX(400));
      // we're done with the `env` variable, so we can use it to store the
      // headers.
      env = rb_ary_entry(response, 1);
      // close connection header
      rb_hash_aset(env, CONNECTION_HEADER, CONNECTION_CLOSE);
    }
    // sends the response and performs the upgrade, if needed
    if (!send_response(request, response))
      Websockets.new(request, rb_ary_entry(response, 3));
    // Registry is a Bag, not a Set. Only the first reference is removed,
    // any added references (if exist) are left in the Registry.
    Registry.remove(response);
    return 0;
  }
  // perform HTTP callback
  response = (VALUE)Server.get_udata(request->server, 0);
  if (response)
    response = RubyCaller.call_unsafe2(response, call_proc_id, 1, &env);
  // review env for highjack and clear request body if true
  if (rb_hash_aref(env, R_HIJACK_IO) != Qnil && TYPE(response) == T_ARRAY)
    rb_ary_store(response, 2, Qnil);
  // clean-up env and register response
  if (Registry.replace(env, response))
    Registry.add(response);
  if (!send_response(request, response))
    perform_generic_upgrade(request, response);
  Registry.remove(response);
  return 0;
}
// The core handler passed on to the HttpProtocol object.
static void on_request(struct HttpRequest* request) {
  // work inside the GVL
  rb_thread_call_with_gvl(handle_request_in_gvl, request);
}

/* ////////////////////////////////////////////////////////////

The main class - Iodine::Http

//////////////////////////////////////////////////////////// */

/////////////////////////////
// server callbacks

// called when the server starts up. Saves the server object to the
// instance.
static void on_init(struct Server* server) {
  VALUE core_instance = ((VALUE)Server.settings(server)->udata);
  // save the updated on_request  as a global value on the server, using
  // fd=0
  if (rb_iv_get(core_instance, "@on_http") != Qnil)
    Server.set_udata(server, 0, (void*)rb_iv_get(core_instance, "@on_http"));
  if (rb_iv_get(core_instance, "@on_websocket") != Qnil)
    Server.set_udata(server, 1,
                     (void*)rb_iv_get(core_instance, "@on_websocket"));
  // message
  VALUE version_val = rb_const_get(rIodine, rb_intern("VERSION"));
  char* version_str = StringValueCStr(version_val);
  fprintf(stderr,
          "Starting up Iodine's HTTP server, V. %s using %d thread%s X %d "
          "processes\n",
          version_str, Server.settings(server)->threads,
          (Server.settings(server)->threads > 1 ? "s" : ""),
          Server.settings(server)->processes);
  // set the server variable in the core server object.. is this GC safe?
  set_server(core_instance, server);
  // perform on_init callback
  VALUE on_start_block = rb_iv_get(core_instance, "on_start_block");
  if (on_start_block != Qnil) {
    RubyCaller.call(on_start_block, call_proc_id);
  }
}

// called when server is idling
static void on_idle(struct Server* srv) {
  // call(reg->obj, on_data_func_id);
  // rb_gc_start();
}

/////////////////////////////
// Running the Http server
//
// the no-GVL state
static void* srv_start_no_gvl(void* _settings) {
  struct ServerSettings* settings = _settings;
  long ret = Server.listen(*settings);
  if (ret < 0)
    perror("Couldn't start server");
  return 0;
}
//
// The stop unblock fun
static void unblck(void* _) {
  Server.stop_all();
}
//
// This method starts the Http server instance.
static VALUE http_start(VALUE self) {
  // review the callbacks
  VALUE on_http_handler = rb_iv_get(self, "@on_http");
  VALUE on_websocket_handler = rb_iv_get(self, "@on_websocket");
  if (on_websocket_handler == Qnil && on_http_handler == Qnil) {
    rb_raise(
        rb_eRuntimeError,
        "Either the `on_http` callback or the `on_websocket` must be defined");
    return Qnil;
  }
  if (on_http_handler != Qnil &&
      !rb_respond_to(on_http_handler, call_proc_id)) {
    rb_raise(rb_eTypeError,
             "The on_http callback should be an object that answers to the "
             "method `call`");
    return Qnil;
  }
  // review the callbacks
  if (on_websocket_handler != Qnil &&
      !rb_respond_to(on_websocket_handler, call_proc_id)) {
    rb_raise(
        rb_eTypeError,
        "The on_websocket callback should be an object that answers to the "
        "method `call`");
    return Qnil;
  }
  // get current settings
  // load the settings from the Ruby layer to the C layer
  VALUE rb_port = rb_ivar_get(self, rb_intern("@port"));
  VALUE rb_bind = rb_ivar_get(self, rb_intern("@address"));
  VALUE rb_timeout = rb_ivar_get(self, rb_intern("@timeout"));
  VALUE rb_threads = rb_ivar_get(self, rb_intern("@threads"));
  VALUE rb_processes = rb_ivar_get(self, rb_intern("@processes"));
  VALUE rb_max_body = rb_ivar_get(self, rb_intern("@max_body_size"));
  VALUE rb_max_msg = rb_ivar_get(self, rb_intern("@max_msg_size"));
  VALUE rb_ws_tout = rb_ivar_get(self, rb_intern("@ws_timeout"));
  VALUE rb_public_folder = rb_ivar_get(self, rb_intern("@public_folder"));
  // validate port
  if (rb_port != Qnil && TYPE(rb_port) != T_FIXNUM) {
    rb_raise(rb_eTypeError, "port isn't a valid number.");
    return Qnil;
  }
  int iport = rb_port == Qnil ? 3000 : FIX2INT(rb_port);
  if (iport > 65535 || iport < 0) {
    rb_raise(rb_eTypeError, "port out of range.");
    return Qnil;
  }
  // validate bind
  if (rb_bind != Qnil && TYPE(rb_bind) != T_STRING) {
    rb_raise(rb_eTypeError, "bind should be either a String or nil.");
    return Qnil;
  }
  if (rb_bind != Qnil)
    rb_warn("the `bind` property is ignored, unimplemented yet");
  // validate timeout
  if (rb_timeout != Qnil &&
      (TYPE(rb_timeout) != T_FIXNUM || (FIX2INT(rb_timeout) > 255) ||
       (FIX2INT(rb_timeout) < 0))) {
    rb_raise(rb_eTypeError,
             "timeout isn't a valid number (any number from 0 to 255).\n       "
             " 0 == no timeout.");
    return Qnil;
  }
  // validate process count and set limits
  if (rb_processes != Qnil && TYPE(rb_processes) != T_FIXNUM &&
      FIX2INT(rb_processes) > 32) {
    rb_raise(rb_eTypeError, "processes isn't a valid number (1-32).");
    return Qnil;
  }
  // validate thread count and set limits
  if (rb_threads != Qnil && TYPE(rb_threads) != T_FIXNUM &&
      FIX2INT(rb_threads) > 128) {
    rb_raise(rb_eTypeError, "threads isn't a valid number (-1 to 128).");
    return Qnil;
  }
  // validate body and message size limits
  int imax_body = rb_max_body == Qnil ? 32 : FIX2INT(rb_max_body);
  if (imax_body > 2048 || imax_body < 0) {
    rb_raise(rb_eTypeError,
             "max_body_size out of range. should be lo less then 0 and no more "
             "then 2048 (2Gb).");
    return Qnil;
  }
  int imax_msg = rb_max_msg == Qnil ? 65536 : FIX2INT(rb_max_msg);
  if (imax_msg > 2097152 || imax_msg < 0) {
    rb_raise(rb_eTypeError,
             "rb_max_msg out of range. should be lo less then 0 and no more "
             "then 2,097,152 (2Mb). Default is 64Kb");
    return Qnil;
  }
  Websockets.max_msg_size = imax_msg;
  // validate ws timeout
  int iwstout = rb_ws_tout == Qnil ? 45 : FIX2INT(rb_ws_tout);
  if (iwstout > 120 || iwstout < 0) {
    rb_raise(rb_eTypeError,
             "ws_timeout out of range. should be lo less then 0 and no more "
             "then 120 (2 minutes). Default is 45 seconds.");
    return Qnil;
  }
  Websockets.timeout = iwstout;
  // validate the public folder type
  if (rb_public_folder != Qnil && TYPE(rb_public_folder) != T_STRING) {
    rb_raise(rb_eTypeError,
             "rb_public_folder should be either a String or nil.");
    return Qnil;
  }

  // make port into a CString (for Lib-Server)
  char port[7];
  char* bind = rb_bind == Qnil ? NULL : StringValueCStr(rb_bind);
  unsigned char timeout = rb_timeout == Qnil ? 2 : FIX2INT(rb_timeout);
  snprintf(port, 6, "%d", iport);
  // create the HttpProtocol object
  struct HttpProtocol http_protocol = HttpProtocol();
  http_protocol.on_request = on_request;
  http_protocol.maximum_body_size = imax_msg;
  http_protocol.public_folder =
      rb_public_folder == Qnil ? NULL : StringValueCStr(rb_public_folder);

  // setup the server
  struct ServerSettings settings = {
      .protocol = (struct Protocol*)(&http_protocol),
      .timeout = timeout,
      .threads = rb_threads == Qnil ? 1 : (FIX2INT(rb_threads)),
      .processes = rb_processes == Qnil ? 1 : (FIX2INT(rb_processes)),
      .on_init = on_init,
      .on_idle = on_idle,
      .port = (iport > 0 ? port : NULL),
      .address = bind,
      .udata = (void*)self,
      .busy_msg =
          "HTTP/1.1 500 Server Too Busy\r\n"
          "Connection: closed\r\n"
          "Content-Length: 15\r\n\r\n"
          "Server Too Busy\r\n",
  };
  // setup some Rack dynamic values
  R_MTHREAD_V = settings.threads > 1 ? Qtrue : Qfalse;
  R_MPROCESS_V = settings.processes > 1 ? Qtrue : Qfalse;
  // rb_thread_call_without_gvl(slow_func, slow_arg, unblck_func,
  // unblck_arg);
  rb_thread_call_without_gvl(srv_start_no_gvl, &settings, unblck, NULL);
  return self;
}

/////////////////////////////
// stuff related to the core inheritance.

// prevent protocol changes
static VALUE http_protocol_get(VALUE self) {
  rb_warn(
      "The Iodine HTTP protocol is written in C, it cannot be edited nor "
      "viewed in Ruby.");
  return self;
}
static VALUE http_protocol_set(VALUE self, VALUE _) {
  return http_protocol_get(self);
}
/////////////////////////////
// initialize the class and the whole of the Iodine/http library
void Init_iodine_http(void) {
  // get IDs and data that's used often
  call_proc_id = rb_intern("call");            // used to call the main callback
  server_var_id = rb_intern("server");         // when upgrading
  fd_var_id = rb_intern("sockfd");             // when upgrading
  new_func_id = rb_intern("new");              // when upgrading
  on_open_func_id = rb_intern("on_open");      // when upgrading
  each_method_id = rb_intern("each");          // for the response
  to_s_method_id = rb_intern("to_s");          // for the response
  _hijack_sym = ID2SYM(rb_intern("_hijack"));  // for hijacking
  rb_global_variable(&_hijack_sym);

  // some common Rack & Http strings
  REQUEST_METHOD = rb_enc_str_new_literal("REQUEST_METHOD", BinaryEncoding);
  rb_global_variable(&REQUEST_METHOD);
  rb_obj_freeze(REQUEST_METHOD);
  CONTENT_TYPE = rb_enc_str_new_literal("CONTENT_TYPE", BinaryEncoding);
  rb_global_variable(&CONTENT_TYPE);
  rb_obj_freeze(CONTENT_TYPE);
  CONTENT_LENGTH = rb_enc_str_new_literal("CONTENT_LENGTH", BinaryEncoding);
  rb_global_variable(&CONTENT_LENGTH);
  rb_obj_freeze(CONTENT_LENGTH);
  SCRIPT_NAME = rb_enc_str_new_literal("SCRIPT_NAME", BinaryEncoding);
  rb_global_variable(&SCRIPT_NAME);
  rb_obj_freeze(SCRIPT_NAME);
  PATH_INFO = rb_enc_str_new_literal("PATH_INFO", BinaryEncoding);
  rb_global_variable(&PATH_INFO);
  rb_obj_freeze(PATH_INFO);
  QUERY_STRING = rb_enc_str_new_literal("QUERY_STRING", BinaryEncoding);
  rb_global_variable(&QUERY_STRING);
  rb_obj_freeze(QUERY_STRING);
  QUERY_ESTRING = rb_str_buf_new(0);
  rb_global_variable(&QUERY_ESTRING);
  rb_obj_freeze(QUERY_ESTRING);
  SERVER_NAME = rb_enc_str_new_literal("SERVER_NAME", BinaryEncoding);
  rb_global_variable(&SERVER_NAME);
  rb_obj_freeze(SERVER_NAME);
  SERVER_PORT = rb_enc_str_new_literal("SERVER_PORT", BinaryEncoding);
  rb_global_variable(&SERVER_PORT);
  rb_obj_freeze(SERVER_PORT);
  SERVER_PORT_80 = rb_enc_str_new_literal("80", BinaryEncoding);
  rb_global_variable(&SERVER_PORT_80);
  rb_obj_freeze(SERVER_PORT_80);
  SERVER_PORT_443 = rb_enc_str_new_literal("443", BinaryEncoding);
  rb_global_variable(&SERVER_PORT_443);
  rb_obj_freeze(SERVER_PORT_443);
  R_VERSION = rb_enc_str_new_literal("rack.version", BinaryEncoding);
  rb_global_variable(&R_VERSION);
  rb_obj_freeze(R_VERSION);
  R_SCHEME = rb_enc_str_new_literal("rack.url_scheme", BinaryEncoding);
  rb_global_variable(&R_SCHEME);
  rb_obj_freeze(R_SCHEME);
  R_SCHEME_HTTP = rb_enc_str_new_literal("http", BinaryEncoding);
  rb_global_variable(&R_SCHEME_HTTP);
  rb_obj_freeze(R_SCHEME_HTTP);
  R_SCHEME_HTTPS = rb_enc_str_new_literal("https", BinaryEncoding);
  rb_global_variable(&R_SCHEME_HTTPS);
  rb_obj_freeze(R_SCHEME_HTTPS);
  R_INPUT = rb_enc_str_new_literal("rack.input", BinaryEncoding);
  rb_global_variable(&R_INPUT);
  rb_obj_freeze(R_INPUT);
  R_ERRORS = rb_enc_str_new_literal("rack.errors", BinaryEncoding);
  rb_global_variable(&R_ERRORS);
  rb_obj_freeze(R_ERRORS);
  R_MTHREAD = rb_enc_str_new_literal("rack.multithread", BinaryEncoding);
  rb_global_variable(&R_MTHREAD);
  rb_obj_freeze(R_MTHREAD);
  R_MPROCESS = rb_enc_str_new_literal("rack.multiprocess", BinaryEncoding);
  rb_global_variable(&R_MPROCESS);
  rb_obj_freeze(R_MPROCESS);
  R_RUN_ONCE = rb_enc_str_new_literal("rack.run_once", BinaryEncoding);
  rb_global_variable(&R_RUN_ONCE);
  rb_obj_freeze(R_RUN_ONCE);
  R_HIJACK_Q = rb_enc_str_new_literal("rack.hijack?", BinaryEncoding);
  rb_global_variable(&R_HIJACK_Q);
  rb_obj_freeze(R_HIJACK_Q);
  R_HIJACK = rb_enc_str_new_literal("rack.hijack", BinaryEncoding);
  rb_global_variable(&R_HIJACK);
  rb_obj_freeze(R_HIJACK);
  R_HIJACK_V = Qnil;  // rb_fdopen(int fd, const char *modestr)
  R_HIJACK_IO = rb_enc_str_new_literal("rack.hijack_io", BinaryEncoding);
  rb_global_variable(&R_HIJACK_IO);
  rb_obj_freeze(R_HIJACK_IO);

  // Iodine variavles
  R_IOFD = rb_enc_str_new_literal("iodine.fd", BinaryEncoding);
  rb_global_variable(&R_IOFD);
  rb_obj_freeze(R_IOFD);
  // VALUE version_val = rb_const_get(rIodine, rb_intern("iodine.version"));
  // R_IOVERSION_V = rb_str_split(version_val, ".");
  // rb_global_variable(&R_IOVERSION_V);

  // setup for Rack version 1.3.
  R_VERSION_V = rb_ary_new_capa(2);
  rb_ary_push(R_VERSION_V, rb_enc_str_new_literal("1", BinaryEncoding));
  rb_ary_push(R_VERSION_V, rb_enc_str_new_literal("3", BinaryEncoding));
  rb_global_variable(&R_VERSION_V);
  R_ERRORS_V = rb_stdout;
  // Websocket Upgrade
  UPGRADE_HEADER = rb_enc_str_new_literal("Upgrade", BinaryEncoding);
  rb_global_variable(&UPGRADE_HEADER);
  rb_obj_freeze(UPGRADE_HEADER);
  CONNECTION_HEADER = rb_enc_str_new_literal("connection", BinaryEncoding);
  rb_global_variable(&CONNECTION_HEADER);
  rb_obj_freeze(CONNECTION_HEADER);
  CONNECTION_CLOSE = rb_enc_str_new_literal("close", BinaryEncoding);
  rb_global_variable(&CONNECTION_CLOSE);
  rb_obj_freeze(CONNECTION_CLOSE);
  WEBSOCKET_STR = rb_enc_str_new_literal("websocket", BinaryEncoding);
  rb_global_variable(&WEBSOCKET_STR);
  rb_obj_freeze(WEBSOCKET_STR);
  WEBSOCKET_VER = rb_enc_str_new_literal("13", BinaryEncoding);
  rb_global_variable(&WEBSOCKET_VER);
  rb_obj_freeze(WEBSOCKET_VER);
  WEBSOCKET_SEC_VER =
      rb_enc_str_new_literal("sec-websocket-version", BinaryEncoding);
  rb_global_variable(&WEBSOCKET_SEC_VER);
  rb_obj_freeze(WEBSOCKET_SEC_VER);
  WEBSOCKET_SEC_EXT =
      rb_enc_str_new_literal("sec-websocket-extensions", BinaryEncoding);
  rb_global_variable(&WEBSOCKET_SEC_EXT);
  rb_obj_freeze(WEBSOCKET_SEC_EXT);
  WEBSOCKET_SEC_ACPT =
      rb_enc_str_new_literal("sec-websocket-accept", BinaryEncoding);
  rb_global_variable(&WEBSOCKET_SEC_ACPT);
  rb_obj_freeze(WEBSOCKET_SEC_ACPT);
  // sendfile - for later...
  XSENDFILETYPE = rb_enc_str_new_literal("X-Sendfile-Type", BinaryEncoding);
  rb_global_variable(&XSENDFILETYPE);
  rb_obj_freeze(XSENDFILETYPE);
  XSENDFILE = rb_enc_str_new_literal("X-Sendfile", BinaryEncoding);
  rb_global_variable(&XSENDFILE);
  rb_obj_freeze(XSENDFILE);

  rb_global_variable(&rServer);
  // define the Http class
  rHttp = rb_define_class_under(rIodine, "Http", rIodine);
  rb_global_variable(&rHttp);
  // add the Http sub-functions
  rb_define_method(rHttp, "protocol=", http_protocol_set, 1);
  rb_define_method(rHttp, "protocol", http_protocol_get, 0);
  rb_define_method(rHttp, "start", http_start, 0);
  rb_define_attr(rHttp, "max_body_size", 1, 1);
  rb_define_attr(rHttp, "max_msg_size", 1, 1);
  rb_define_attr(rHttp, "ws_timeout", 1, 1);
  rb_define_attr(rHttp, "on_http", 1, 1);
  rb_define_attr(rHttp, "on_websocket", 1, 1);
  rb_define_attr(rHttp, "public_folder", 1, 1);
  // initialize the RackIO class
  RackIO.init();
  // initialize the Websockets class
  Websockets.init();
}
