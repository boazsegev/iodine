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
  #
  # The `on_http` handler can set special `env` hash keys to "upgrade" the HTTP connection to a websocket connection or any other protocol.
  class Http
    # The WebsocketProtocol module is used as a mixin for any websocket connection, much like {Protocol} is used for generic connections.
    #
    # When the {Iodine::Http#on_http} function returns a four-member array (intead of the usual Rack array, that has only three members),
    # the fourth member will be used as a websocket connection handler and it will inherit all the methods from this module.
    #
    # <b>For some reason, the following methods do not show in the documentation:</b> The links refer to the same methods as they apply to the {Iodine::Protocol} mixin:
    #
    # * {Iodine::Protocol#run}
    # * {Iodine::Protocol#run_every}
    # * {Iodine::Protocol#run_after}
    # * {Iodine::Protocol#defer}
    #
    module WebsocketProtocol
    end
  end
end
