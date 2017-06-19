# This is a Websocket echo application.
#
# Running this application from the command line is eacy with:
#
#      iodine hello.ru
#
# Or, in single thread and single process:
#
#      iodine -t 1 -w 1 hello.ru
#
# Benchmark with `ab` or `wrk` (a 5 seconds benchmark with 2000 concurrent clients):
#
#      ab -c 2000 -t 5 -n 1000000 -k http://127.0.0.1:3000/
#      wrk -c2000 -d5 -t12 http://localhost:3000/


# A simple router - Checks for Websocket Upgrade and answers HTTP.
module MyHTTPRouter
  # This is the HTTP response object according to the Rack specification.
  HTTP_RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '32' },
   ['Please connect using websockets.']]

  WS_RESPONSE = [0, {}, []].freeze

   # this is function will be called by the Rack server (iodine) for every request.
   def self.call env
     # check if this is an upgrade request.
     if(env['upgrade.websocket?'.freeze])
       env['upgrade.websocket'.freeze] = WebsocketBroadcast
       return WS_RESPONSE
     end
     # simply return the RESPONSE object, no matter what request was received.
     HTTP_RESPONSE
   end
end

# A simple Websocket Callback Object.
class WebsocketBroadcast
  # seng a message to new clients.
  def on_open
    puts self
    write "Welcome to our echo service!"
  end
  # send a message, letting the client know the server is suggunt down.
  def on_shutdown
    write "Server shutting down. Goodbye."
  end
  # perform the echo
  def on_message data
    write data
  end
end

# this function call links our HelloWorld application with Rack
run MyHTTPRouter
