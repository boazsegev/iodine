require 'iodine'

class Iodine
  # The Http class and it's subclasses inherits from the core {Iodine} class and {Protocol}, so that all the methods
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

    # The WebsocketProtocol module is used as a mixin for any websocket connection, much like {Protocol} is used for generic connections.
    #
    # When the {Iodine::Http#on_websocket} function returns a four-member array (intead of the usual Rack array, that has only three members),
    # the fourth member will be used as a websocket connection handler and it will inherit all the methods from this module.
    module WebsocketProtocol


    end

  end
end
