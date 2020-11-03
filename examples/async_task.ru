# This is a task scheduling WebSocket push example application.
#
# Benchmark HTTPP with `ab` or `wrk` (a 5 seconds benchmark with 2000 concurrent clients):
#
#      ab -c 2000 -t 5 -n 1000000 -k http://127.0.0.1:3000/
#      wrk -c2000 -d5 -t12 http://localhost:3000/
#
# Test websocket tasks using the browser. For example:
#      ws = new WebSocket("ws://localhost:3000/userID"); ws.onmessage = function(e) {console.log(e.data);}; ws.onclose = function(e) {console.log("closed")};
#      ws.onopen = function(e) {ws.send(JSON.stringify({'task': 'echo', 'data': 'Hello!'}));};
require 'iodine'
require 'json'

TASK_PUBLISHING_ENGINE = Iodine::PubSub::PROCESS

# This module handles tasks and send them back to the frontend
module TaskHandler
  def echo msg
    msg = Iodine::JSON.parse(msg, symbolize_names: true)
    publish_to = msg.delete(:from)
    Iodine.publish(publish_to, msg.to_json, TASK_PUBLISHING_ENGINE) if publish_to
    puts "performed 'echo' task"
  rescue => e
    puts "JSON task message error? #{e.message} - under attack?"
  end

  def add msg
    msg = Iodine::JSON.parse(msg, symbolize_names: true)
    raise "addition task requires an array of numbers" unless msg[:data].is_a?(Array)
    msg[:data] = msg[:data].inject(0){|sum,x| sum + x }
    publish_to = msg.delete(:from)
    Iodine.publish(publish_to, msg.to_json, TASK_PUBLISHING_ENGINE) if publish_to
    puts "performed 'add' task"
  rescue => e
    puts
     "JSON task message error? #{e.message} - under attack?"
  end

  def listen2tasks
    Iodine.subscribe(:echo) {|ch,msg| TaskHandler.echo(msg) }
    Iodine.subscribe(:add) {|ch,msg| TaskHandler.add(msg) }
  end

  extend self
end

module WebsocketClient
  def on_open client
    # Pub/Sub directly to the client (or use a block to process the messages)
    client.subscribe client.env['PATH_INFO'.freeze]
  end
  def on_message client, data
    # Strings and symbol channel names are equivalent.
    msg = Iodine::JSON.parse(data, symbolize_names: true)
    raise "no valid task" unless ["echo".freeze, "add".freeze].include? msg[:task]
    msg[:from] = client.env['PATH_INFO'.freeze]
    client.publish msg[:task], msg.to_json, TASK_PUBLISHING_ENGINE
  rescue => e
      puts "JSON message error? #{e.message}\n\t#{data}\n\t#{msg}"
  end
  extend self
end

APP = Proc.new do |env|
  if env['rack.upgrade?'.freeze] == :websocket 
    env['rack.upgrade'.freeze] = WebsocketClient 
    [0,{}, []] # It's possible to set cookies for the response.
  elsif env['rack.upgrade?'.freeze] == :sse
    puts "SSE connections can only receive data from the server, the can't write." 
    env['rack.upgrade'.freeze] = WebsocketClient
    [0,{}, []] # It's possible to set cookies for the response.
  else
    [200, {"Content-Type" => "text/plain"}, ["Send messages with WebSockets using JSON.\ni.e.: {\"task\":\"add\", \"data\":[1,2]}"]]
  end
end

# test automatically for Redis extensions.
if(Iodine::PubSub.default.is_a? Iodine::PubSub::Redis)
  TASK_PUBLISHING_ENGINE = Iodine::PubSub.default
  if(ARGV.include? "worker")
    TaskHandler.listen2tasks
    Iodine.workers = 1
    Iodine.threads = 16 if Iodine.threads == 0
    Iodine.start
    exit(0)
  end
else
  TaskHandler.listen2tasks
end

# # or in config.ru
run APP
