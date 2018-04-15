module Iodine
  # This module lists the available API for WebSocket and EventSource (SSE) connections.
  #
  # This module is mixed in (using `extend` and `include`) with the WebSocket Callback Object (as specified by the {file:SPEC-Websocket-Draft.md proposed Rack specification}.
  #
  # The server performs `extend` to allow the application to be namespace agnostic (so the server can be replaced without effecting the application).
  #
  # The websocket API is divided into three main groups:
  # * Server <=> Client relations ({Iodine::Websocket#write}, {Iodine::Websocket#close} etc')
  # * Client <=> Server <=> Pub/Sub relations ({Iodine::Websocket#subscribe}, {Iodine::Websocket#publish}.
  # * Task scheduling ({Iodine::Websocket.defer}, {Iodine::Websocket#defer}, {Iodine::Websocket.each}).
  #
  # Notice that Websocket callback objects (as specified by the {file:SPEC-Websocket-Draft.md proposed Rack specification} *MUST* provide an `on_message(data)` callback.
  module Websocket
  end
end
