# This example implements a Redis pub/sub engine according to the Iodine::PubSub::Engine specifications.
#
# The engine code is locates at examples/redis_pubsub.rb and it requires the hiredis gem.
#
# Run this applications on two ports, in two terminals to see the synchronization is action
#
#      REDIS_URL=redis://localhost:6379/0 iodine -t 1 -p 3000 redis.ru
#      REDIS_URL=redis://localhost:6379/0 iodine -t 1 -p 3030 redis.ru
#
require 'uri'
# initialize the Redis engine for each Iodine process by using `Iodine.run`
if ENV["REDIS_URL"]
  uri = URI(ENV["REDIS_URL"])
  Iodine.default_pubsub = Iodine::PubSub::RedisEngine.new(uri.host, uri.port, 0, uri.password)
else
  puts "* No Redis! pub/sub is limited to the process cluster."
end
puts "The default Pub/Sub engine is:", Iodine.default_pubsub

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
     if(env['upgrade.websocket?'.freeze])
       env['upgrade.websocket'.freeze] = WS_RedisPubSub.new(env['PATH_INFO'] ? env['PATH_INFO'][1..-1] : "guest")
       return [0, {}, []]
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
  def on_open
    subscribe channel: "chat"
    # let everyone know we arrived
    # publish channel: "chat", message: "#{@name} entered the chat."
  end
  # send a message, letting the client know the server is suggunt down.
  def on_shutdown
    write "Server shutting down. Goodbye."
  end
  # perform the echo
  def on_message data
    publish channel: "chat", message: "#{@name}: #{data}"
  end
  def on_close
    # let everyone know we left
    publish channel: "chat", message: "#{@name} left the chat."
    # we don't need to unsubscribe, subscriptions are cleared automatically once the connection is closed.
  end
end

# this function call links our HelloWorld application with Rack
run MyHTTPRouter
