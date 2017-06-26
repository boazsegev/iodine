module Iodine
  # This module lists the available API for Websocket connections and classes.
  #
  # This module is mixed in (using `extend` and `include`) with the Websocket Callback Object (as specified by the {file:SPEC-Websocket-Draft.md proposed Rack specification}.
  #
  # The websocket API is divided into three main groups:
  # * Server <=> Client relations ({Iodine::Websocket#write}, {Iodine::Websocket#close} etc')
  # * Client <=> Server <=> Client relations ({Iodine::Websocket#subscribe}, {Iodine::Websocket#publish}, etc').
  # * Task scheduling ({Iodine::Websocket.defer}, {Iodine::Websocket#defer}, {Iodine::Websocket.each}).
  #
  # Notice that Websocket callback objects (as specified by the {file:SPEC-Websocket-Draft.md proposed Rack specification} *MUST* provide an `on_message(data)` callback.
  module Websocket
  end
end
