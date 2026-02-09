# # Written for NeoRack with Iodine v.0.8.x
# #
# # Run using:
# iodine -t <number of threads> -w <number of processes> -p <port>
# # i.e.:
# iodine -t 8 -w 0 -p 3334

# local process cluster support is built into iodine's pub/sub, but cross machine pub/sub requires Redis.
class ShootoutApp
  SHOOTOUT = "shootout".freeze
  def on_http(e)
    e.finish "This application should be used with the websocket-shootout benchmark utility."
  end
  
  def on_open(e)
    e.subscribe channel: SHOOTOUT
  end

  def on_message e, data

    if data[0] == 'b' # binary
      e.publish(channel: SHOOTOUT, message: data)
      data[0] = 'r'
      e.write data
      return
    end

    # cmd, payload = JSON(data).values_at('type', 'payload')
    cmd, payload = Iodine::JSON.parse(data).values_at('type', 'payload')
    if cmd == 'echo'
      e.write({type: 'echo', payload: payload}.to_json)
    else
      e.publish(channel: SHOOTOUT, message: {type: 'broadcast', payload: payload})
      e.write({type: "broadcastResult", payload: payload})
    end

  rescue
    puts "Incoming message format error!"
  end

end

run ShootoutApp.new
