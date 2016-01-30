#include "iodine_websocket.h"
#include <ruby/io.h>

/* ////////////////////////////////////////////////////////////
This file creates an HTTP server based on the Iodine libraries.

The server is (mostly) Rack compatible, except:

1. upgrade requests are handled using special upgrade handlers.
2. if a String is returned, it is a assumed to be a status 200 Html data?

//////////////////////////////////////////////////////////// */

//////////////
// general global definitions we will use herein.
static VALUE rWebsocket;    // The Iodine::Http::Websocket class
static ID server_var_id;    // id for the Server variable (pointer)
static ID fd_var_id;        // id for the file descriptor (Fixnum)
static ID call_proc_id;     // id for `#call`
static ID new_func_id;      // id for the Class.new method
static ID on_open_func_id;  // the on_open callback's ID
static char ws_service_name[] = "websocket";

struct WebsocketProtocol {
  struct Protocol protocol;
  VALUE handler;
  VALUE buffer;
};

static struct WebsocketProtocol* WebsocketProtocol_new(void) {
  struct WebsocketProtocol* ws = malloc(sizeof(struct WebsocketProtocol));
  *ws = (struct WebsocketProtocol){
      .protocol.service = ws_service_name,
  };
  return ws;
}
static void WebsocketProtocol_destroy(struct WebsocketProtocol* ws) {
  if (ws->handler)
    Registry.remove(ws->handler);
  if (ws->buffer)
    Registry.remove(ws->buffer);
  free(ws);
}
/*
# review handshake (version, extentions)
# should consider adopting the websocket gem for handshake and framing:
# https://github.com/imanel/websocket-ruby
# http://www.rubydoc.info/github/imanel/websocket-ruby
return refuse response unless handler || handler == true
io = request[:io]
response.keep_alive = true
response.status = 101
response['upgrade'.freeze] = 'websocket'.freeze
response['content-length'.freeze] = '0'.freeze
response['connection'.freeze] = 'Upgrade'.freeze
response['sec-websocket-version'.freeze] = '13'.freeze
# Note that the client is only offering to use any advertised extensions
# and MUST NOT use them unless the server indicates that it wishes to use
the
extension.
ws_extentions = []
ext = []
request['sec-websocket-extensions'.freeze].to_s.split(/[\s]*[,][\s]*\/.freeze).each
{|ex| ex = ex.split(/[\s]*;[\s]*\/.freeze); ( ( tmp = SUPPORTED_EXTENTIONS[
ex[0] ].call(ex[1..-1]) ) && (ws_extentions << tmp) && (ext << tmp.name) )
if
SUPPORTED_EXTENTIONS[ ex[0] ] }
ext.compact!
if ext.any?
  response['sec-websocket-extensions'.freeze] = ext.join(', '.freeze)
else
  ws_extentions = nil
end
response['sec-websocket-accept'.freeze] =
Digest::SHA1.base64digest(request['sec-websocket-key'.freeze] +
'258EAFA5-E914-47DA-95CA-C5AB0DC85B11'.freeze)
response.session
# Iodine.log "#{@request[:client_ip]} [#{Time.now.utc}] -
#{@connection.object_id} Upgraded HTTP to WebSockets.\n"
response.finish
self.new(io.io, handler: handler, request: request, ext: ws_extentions)
return true
*/

// This should be called within the GVL, as it performs Ruby API calls
void websocket_new(struct HttpRequest* request, VALUE handler) {
  struct WebsocketProtocol* ws = NULL;
  int todo_manage_handshake_and_all;
  // placeholeder until implementation is written.
  goto reject;
  // check that we actually have a websocket handler
  if (handler == Qnil || handler == Qfalse)
    goto reject;
  // create the Websocket Protocol
  ws = WebsocketProtocol_new();
  if (!ws)
    goto reject;
  // make sure we have a valid handler, with the Websocket Protocol mixin.
  if (TYPE(handler) == T_CLASS) {
    // include the Protocol module
    // // do we neet to check?
    // if (rb_mod_include_p(handler, rWebsocket) == Qfalse)
    rb_include_module(handler, rWebsocket);
    handler = RubyCaller.call_unsafe(handler, new_func_id);
    // check that we created a handler
    if (handler == Qnil || handler == Qfalse)
      goto reject;
  } else {
    // include the Protocol module in the object's class
    VALUE p_class = rb_obj_class(handler);
    // // do we neet to check?
    // if (rb_mod_include_p(handler, rWebsocket) == Qfalse)
    rb_include_module(p_class, rWebsocket);
  }
  // set the Ruby handler for websocket messages
  ws->handler = handler;
  ws->buffer = rb_str_buf_new(2048);
  Registry.add(handler);
  Registry.add(ws->buffer);
  Server.set_protocol(request->server, request->sockfd, (struct Protocol*)ws);
  // set the server and fd values for the handler... (used for `write` and
  // `close`)
  rb_ivar_set(handler, fd_var_id, INT2FIX(request->sockfd));
  set_server(handler, request->server);
  // call the on_open callback
  RubyCaller.call_unsafe(handler, on_open_func_id);

reject:
  if (ws)
    WebsocketProtocol_destroy(ws);
  Websockets.reject(request);
  return;
}
void websocket_reject(struct HttpRequest* request) {
  static char bad_req[] =
      "HTTP/1.1 400 Bad HttpRequest\r\n"
      "Connection: closed\r\n"
      "Content-Length: 16\r\n\r\n"
      "Bad HTTP Request\r\n";
  Server.write(request->server, request->sockfd, bad_req, sizeof(bad_req));
  Server.close(request->server, request->sockfd);
  return;
}

/////////////////////////////
// initialize the class and the whole of the Iodine/http library
void Init_websocket(void) {
  // get IDs and data that's used often
  call_proc_id = rb_intern("call");        // used to call the main callback
  server_var_id = rb_intern("server");     // when upgrading
  fd_var_id = rb_intern("sockfd");         // when upgrading
  new_func_id = rb_intern("new");          // when upgrading
  on_open_func_id = rb_intern("on_open");  // when upgrading

  // the Ruby websockets protocol class.
  rWebsocket = rb_define_module_under(rHttp, "WebsocketProtocol");
  // // callbacks and handlers
  // rb_define_method(rWebsocket, "on_open", empty_func, 0);
  // rb_define_method(rWebsocket, "on_data", def_dyn_data, 0);
  // rb_define_method(rWebsocket, "on_message", def_dyn_message, 1);
  // rb_define_method(rWebsocket, "ping", no_ping_func, 0);
  // rb_define_method(rWebsocket, "on_shutdown", empty_func, 0);
  // rb_define_method(rWebsocket, "on_close", empty_func, 0);
  // // helper methods
  // add_helper_methods(rWebsocket);
  // rb_define_method(rWebsocket, "write", srv_write, 1);
  // rb_define_method(rWebsocket, "close", srv_close, 0);
  // rb_define_method(rWebsocket, "each", srv_close, 0);
}

struct __Websockets__CLASS__ Websockets = {
    .init = Init_websocket,
    .new = websocket_new,
    .reject = websocket_reject,
};
