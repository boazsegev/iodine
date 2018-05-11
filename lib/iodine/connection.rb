module Iodine
  # Iodine's {Iodine::Connection} class is the class that TCP/IP, WebSockets and SSE connections inherit from.
  #
  # Instances of this class are passed to the callback objects. i.e.:
  #
  #     module MyConnectionCallbacks
  #       def on_open client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       def on_message client, data
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       def on_shutdown client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       def on_close client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #       def on_drained client
  #          client.is_a?(Iodine::Connection) # => true
  #       end
  #     end
  #
  # All connection related actions can be performed using the methods provided through this class.
	class Connection
	end
end
