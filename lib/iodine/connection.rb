module Iodine

  # The default connection settings used by {Iodine.listen} and {Iodine.connect}.
  #
  # It's a Hash object that allows Iodine default values to be manipulated. i.e.:
  #
  #       DEFAULT_SETTINGS[:port] = "8080" # replaces the default port, which is `ENV["port"] || "3000"`.
  DEFAULT_SETTINGS = {}

  # @deprecated use {Iodine::DEFAULT_SETTINGS}.
  #
  # The default connection settings used by {Iodine.listen} and {Iodine.connect}.
  DEFAULT_HTTP_ARGS = DEFAULT_SETTINGS

  # Iodine's {Iodine::Connection} class is the class that TCP/IP, WebSockets and SSE connections inherit from.
  #
  # Instances of this class are passed to the callback objects. i.e.:
  #
  #     module MyConnectionCallbacks
  #
  #       # called when the callback object is linked with a new client
  #       def on_open client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #
  #       # called when data is available
  #       def on_message client, data
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #
  #       # called when the server is shutting down, before closing the client
  #       # (it's still possible to send messages to the client)
  #       def on_shutdown client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #
  #       # called when the client is closed (no longer available)
  #       def on_close client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #
  #       # called when all the previous calls to `client.write` have completed
  #       # (the local buffer was drained and is now empty)
  #       def on_drained client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #
  #       # called when timeout was reached, llowing a `ping` to be sent
  #       def ping client
  #          client.is_a?(Iodine::Connection) # => true
  #          clint.close() # close connection on timeout is the default
  #       end
  #
  #       # Allows the module to be used as a static callback object (avoiding object allocation)
  #       extend self
  #     end
  #
  # All connection related actions can be performed using the methods provided through this class.
	class Connection
	end
end
