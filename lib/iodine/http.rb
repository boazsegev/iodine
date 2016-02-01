require 'iodine'

class Iodine
  # The Http class inherits from the core {Iodine} class, so that all the methods
  # and properties of the Iodine class (except the `protocol` and `busy_msg` properties) can be used.
  #
  # Setting the `busy_msg` property will be quitely ignored.
  # Setting the protocol property will print a warning before being ignored.
  #
  # In addition, the following properties are introduced:
  #
  # on_http:: set (or get) the handler for HTTP requests. This handler should behave like a Rack application object.
  # on_websocket:: set (or get) the handler for special Websocket upgrade requests. This handler should behave like a Rack application object and return an array with four (4) objects, the last object being the connection's persistent websocket handler (see {WebsocketProtocol}).
  class Http


  end
end
