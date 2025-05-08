# A simple Rack application that prints out the `env` variable to the browser.
#
# This example also supports the (deprecated) old-style iodine Rack upgrade.
module App
  def self.call(env)
    txt = []
    # support old-style iodine Rack upgrade for pure Rack applications.
    if env['rack.upgrade?']
      env['rack.upgrade'] = self
    else
      env.each {|k,v| txt << "#{k}: #{v}\r\n" }
    end
    [200, {}, txt]
  end
  def self.on_open(e)
    e.subscribe :broadcast
  end
  def self.on_message(e, m)
    Iodine.publish :broadcast, m
  end
end

run App

# Benchmark with keep-alive:
# 
#     ab -c 200 -t 4 -n 1000000 -k http://127.0.0.1:3000/
#     wrk -c200 -d4 -t2 http://localhost:3000/
# 
# Connect to chat server with WebSockets:
# 
#     ws = new WebSocket("ws://" + document.location.host + document.location.pathname);
#     ws.onmessage = function(e) {console.log("Got message!"); console.log(e.data);};
#     ws.onclose = function(e) {console.log("closed")};
#     ws.onopen = function(e) {ws.send("hi");};
# 
# Listen to chat messages with Server Sent Events (EventSource / SSE):
# 
#     const listener = new EventSource(document.location.href);
#     listener.onmessage = (e) => { console.log(e); }
#     listener.addEventListener("time", (e) => { console.log(e); })
#     listener.addEventListener("event", (e) => { console.log(e); })
#
# To run any code / configuration on the root (master) process, call Iodine with `-C`:
#
#     iodine -C config.rb
#
