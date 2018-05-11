module Iodine
  # Iodine's {Iodine::Connection} class is the class that TCP/IP, WebSockets and SSE connections inherit from.
  #
  # Instances of this class are passed to the callback objects. i.e.:
  #
  #     module MyConnectionCallbacks
  #       # called when the callback object is linked with a new client
  #       def on_open client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       # called when data is available
  #       def on_message client, data
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       # called when the server is shutting down, before closing the client
  #       # (it's still possible to send messages to the client)
  #       def on_shutdown client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       # called when the client is closed (no longer available)
  #       def on_close client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       # called when all the previous calls to `client.write` have completed
  #       # (the local buffer was drained and is now empty)
  #       def on_drained client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #     end
  #
  # All connection related actions can be performed using the methods provided through this class.
	class Connection
	end
end
