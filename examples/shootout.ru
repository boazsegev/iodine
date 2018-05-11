require 'iodine'
require 'json'

module ShootoutApp
  # the default HTTP response
  def self.call(env)
     if(env['rack.upgrade?'.freeze] == :websocket)
      env['rack.upgrade'.freeze] = ShootoutApp
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
  def self.on_open client
      client.subscribe :shootout_b, as: :binary
      client.subscribe :shootout
  end
  def self.on_message client, data
    if data[0] == 'b' # binary
        client.publish :shootout_b, data
      data[0] = 'r'
      client.write data
      return
    end
    cmd, payload = JSON(data).values_at('type', 'payload')
    if cmd == 'echo'
      client.write({type: 'echo', payload: payload}.to_json)
    else
      client.publish :shootout, {type: 'broadcast', payload: payload}.to_json
      client.write({type: "broadcastResult", payload: payload}.to_json)
    end
  end
end

run ShootoutApp
#
# def cycle
#   puts `websocket-bench broadcast ws://127.0.0.1:3000/ --concurrent 10 --sample-size 100 --server-type binary --step-size 1000 --limit-percentile 95 --limit-rtt 250ms --initial-clients 1000`
#   sleep(4)
#   puts `wrk -c4000 -d15 -t2 http://localhost:3000/`
#   true
# end
# sleep(10) while cycle

# # Used when debugging:
# ON_IDLE = proc { Iodine::Base.db_print_protected_objects ; Iodine.on_idle(&ON_IDLE) }
# ON_IDLE.call
# Iodine.on_shutdown { Iodine::Base.db_print_protected_objects }

