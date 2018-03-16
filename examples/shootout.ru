require 'iodine'

# ON_IDLE = proc { Iodine::Base.db_print_registry ; Iodine.on_idle(&ON_IDLE) }
# ON_IDLE.call

class ShootoutApp
  # the default HTTP response
  def self.call(env)
    if env['upgrade.websocket?'.freeze] # && env['HTTP_UPGRADE'.freeze] =~ /websocket/i
      env['upgrade.websocket'.freeze] = ShootoutApp.new
      return [0, {}, []]
    end
    out = []
    len = 0
    out << "ENV:\n"
    len += 5
    env.each { |k, v| out << "#{k}: #{v}\n" ; len += out[-1].length }
    request = Rack::Request.new(env)
    out << "\nRequest Path: #{request.path_info}\n"
    len += out[-1].length 
    unless request.params.empty?
      out << "Params:\n"
      len += out[-1].length 
      request.params.each { |k,v| out << "#{k}: #{v}\n" ; len += out[-1].length }
    end
    [200, { 'Content-Length' => len.to_s, 'Content-Type' => 'text/plain; charset=UTF-8;' }, out]
  end
  # We'll base the shootout on the internal Pub/Sub service.
  # It's slower than writing to every socket a pre-parsed message, but it's closer
  # to real-life implementations.
  def on_open
    subscribe channel: "shootout"
  end
  def on_message data
    if data[0] == 'b' # binary
      publish(channel: "shootout", message: data)
      data[0] = 'r'
      write data
      return
    end
    cmd, payload = JSON(data).values_at('type', 'payload')
    if cmd == 'echo'
      write({type: 'echo', payload: payload}.to_json)
    else
      # data = {type: 'broadcast', payload: payload}.to_json
      # broadcast :push2client, data
      publish(channel: "shootout", message: ({type: 'broadcast', payload: payload}.to_json))
      write({type: "broadcastResult", payload: payload}.to_json)
    end
  rescue
    puts "Incoming message format error - not JSON?"
  end
end

run ShootoutApp
#
# def cycle
#   puts `websocket-bench broadcast ws://127.0.0.1:3000/ --concurrent 10 --sample-size 100 --server-type binary --step-size 1000 --limit-percentile 95 --limit-rtt 250ms --initial-clients 1000`
#   sleep(2)
#   puts `wrk -c4000 -d15 -t12 http://localhost:3000/`
#   true
# end
# sleep(10) while cycle
