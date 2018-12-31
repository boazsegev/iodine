# This example implements a Redis pub/sub engine according to the Iodine::PubSub::Engine specifications.
#
# Run this applications on two ports, in two terminals to see the synchronization is action:
#
#      IODINE_REDIS_URL=redis://localhost:6379/0 iodine -t 1 -p 3000 redis.ru
#      IODINE_REDIS_URL=redis://localhost:6379/0 iodine -t 1 -p 3030 redis.ru
#
# Or:
#
#      iodine -t 1 -p 3000 redis.ru -redis redis://localhost:6379/0
#      iodine -t 1 -p 3030 redis.ru -redis redis://localhost:6379/0
#
require 'iodine'
# initialize the Redis engine for each Iodine process.
if Iodine::DEFAULT_HTTP_ARGS[:redis_]
  puts "* Redis support automatically detected."
else
  puts "* No Redis, it's okay, pub/sub will support the process cluster."
end

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
       env['rack.upgrade'.freeze] = WS_RedisPubSub.new(env['PATH_INFO'] && env['PATH_INFO'].length > 1 ? env['PATH_INFO'][1..-1] : "guest")
       return WS_RESPONSE
     end
     # simply return the RESPONSE object, no matter what request was received.
     HTTP_RESPONSE
   end
end

# A simple Websocket Callback Object.
class WS_RedisPubSub
  def initialize name
    @name = name
  end
  # seng a message to new clients.
  def on_open client
    client.subscribe "chat"
    # let everyone know we arrived
    client.publish "chat", "#{@name} entered the chat."
  end
  # send a message, letting the client know the server is suggunt down.
  def on_shutdown client
    client.write "Server shutting down. Goodbye."
  end
  # perform the echo
  def on_message client, data
    client.publish "chat", "#{@name}: #{data}"
  end
  def on_close client
    # let everyone know we left
    client.publish "chat", "#{@name} left the chat."
    # we don't need to unsubscribe, subscriptions are cleared automatically once the connection is closed.
  end
end

# this function call links our HelloWorld application with Rack
run MyHTTPRouter
