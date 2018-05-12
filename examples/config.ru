# This is a WebSocket / SSE notification broadcasting application.
#
# Running this application from the command line is easy with:
#
#      iodine
#
# Or, in single thread and single process:
#
#      iodine -t 1 -w 1
#
# Benchmark with `ab` or `wrk` (a 5 seconds benchmark with 2000 concurrent clients):
#
#      ab -c 2000 -t 5 -n 1000000 -k http://127.0.0.1:3000/
#      wrk -c2000 -d5 -t12 http://localhost:3000/


# A simple router - Checks for Websocket Upgrade and answers HTTP.
module MyHTTPRouter
  # This is the HTTP response object according to the Rack specification.
  HTTP_RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '77' },
   ['Please connect using WebSockets or SSE (send messages only using WebSockets).']]

  WS_RESPONSE = [0, {}, []].freeze

   # this is function will be called by the Rack server (iodine) for every request.
   def self.call env
     # check if this is an upgrade request (WebsSocket / SSE).
     if(env['rack.upgrade?'.freeze])
       env['rack.upgrade'.freeze] = BroadcastClient
       return WS_RESPONSE
     end
     # simply return the RESPONSE object, no matter what request was received.
     HTTP_RESPONSE
   end
end

# A simple Websocket Callback Object.
module BroadcastClient
  # seng a message to new clients.
  def on_open client
    client.subscribe :broadcast
  end
  # send a message, letting the client know the server is suggunt down.
  def on_shutdown client
    client.write "Server shutting down. Goodbye."
  end
  # perform the echo
  def on_message client, data
    client.publish :broadcast, data
  end
  extend self
end

# this function call links our HelloWorld application with Rack
run MyHTTPRouter
