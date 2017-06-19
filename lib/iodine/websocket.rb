module Iodine
  # This module lists the available API for Websocket connections and classes.
  #
  # This module is mixed in (using `extend` and `include`) with the Websocket Callback Object (as specified by the {file:SPEC-Websocket-Draft.md proposed Rack specification}.
  #
  # The websocket API is divided into three main groups:
  # * Server <=> Client relations ({#write}, {#close} etc')
  # * Client <=> Server <=> Client relations ({#subscribe}, {#publish}, etc').
  # * Task scheduling ({Iodine::Websocket.defer}, {#defer}, {Iodine::Websocket.each}).
  module Websocket
  end
end
