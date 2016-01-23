require 'iodine'
require 'iodine/iodine_http'


class Iodine
  # The Http class inherits from the core {Iodine} class, so that all the methods
  # and properties of the Iodine class (except the `protocol` and `busy_msg` properties) can be used.
  #
  # Setting the `busy_msg` property will be quitely ignored.
  # Setting the protocol property will print a warning before being ignored.
  #
  # In addition, the `on_request` property is introduced.
  # `on_request` MUST be an object that answers to `call`.
  #
  # `on_request`'s `#call` will receives a Rack-compatible `env` object and should return a Rack compatible response.
  class Http
  end
end
