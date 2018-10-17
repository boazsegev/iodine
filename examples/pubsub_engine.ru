# This example implements a custom (noop) pub/sub engine according to the Iodine::PubSub::Engine specifications.
#
require 'uri'
require 'iodine'

# creates an example Pub/Sub Engine that merely reports any pub/sub events to the system's terminal
class PubSubReporter < Iodine::PubSub::Engine
  def initialize
    # make sure engine setup is complete
    super
    # register engine and make it into the new default
    @target = Iodine::PubSub.default
    Iodine::PubSub.default = self
    Iodine::PubSub.attach self
  end
  def subscribe to, as = nil
    puts "* Subscribing to \"#{to}\" (#{as || "exact match"})"
  end
  def unsubscribe to, as = nil
    puts "* Unsubscribing to \"#{to}\" (#{as || "exact match"})"
  end
  def publish to, msg
    puts "* Publishing to \"#{to}\": #{msg.to_s[0..12]}..."
    # we need to forward the message to the actual engine (the previous default engine),
    # or it will never be received by any Pub/Sub client.
    @target.publish to, msg
  end
end

PubSubReporter.new

# A simple router - Checks for Websocket Upgrade and answers HTTP.
module MyHTTPRouter
  # This is the HTTP response object according to the Rack specification.
  HTTP_RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '32' },
   ['Please connect using websockets.']]

   WS_RESPONSE = [0, {}, []]

   # this is function will be called by the Rack server (iodine) for every request.
   def self.call env
     # check if this is an upgrade request.
     if(env['rack.upgrade?'.freeze])
       puts "SSE connections will not be able te send data, just listen." if(env['rack.upgrade?'.freeze] == :sse)
       env['rack.upgrade'.freeze] = PubSubClient.new(env['PATH_INFO'] && env['PATH_INFO'].length > 1 ? env['PATH_INFO'][1..-1] : "guest")
       return WS_RESPONSE
     end
     # simply return the RESPONSE object, no matter what request was received.
     HTTP_RESPONSE
   end
end

# A simple Websocket Callback Object.
class PubSubClient
  def initialize name
    @name = name
  end
  # seng a message to new clients.
  def on_open(client)
    client.subscribe "chat"
    # let everyone know we arrived
    client.publish "chat", "#{@name} entered the chat."
  end
  # send a message, letting the client know the server is suggunt down.
  def on_shutdown(client)
    client.write "Server shutting down. Goodbye."
  end
  # perform the echo
  def on_message(client, data)
    client.publish "chat", "#{@name}: #{data}"
  end
  def on_close(client)
    # let everyone know we left
    client.publish "chat", "#{@name} left the chat."
    # we don't need to unsubscribe, subscriptions are cleared automatically once the connection is closed.
  end
end

# this function call links our HelloWorld application with Rack
run MyHTTPRouter
