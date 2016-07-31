# The Rack Application
class MyRackApplication
  # Rack applications use the `call` callback to handle HTTP requests.
  def self.call(env)
    # if upgrading...
    if env['HTTP_UPGRADE'.freeze] =~ /websocket/i
      # assign a class that implements instance callbacks.
      env['iodine.websocket'.freeze] = MyWebsocket.new(env)
      return [0, {}, []]
    end
    # a semi-regualr HTTP response
    out = File.open File.expand_path('../www/index.html', __FILE__)
    [200, { 'X-Sendfile' => File.expand_path('../www/index.html', __FILE__), 'Content-Length' => out.size }, out]
  end
end

###################
## Websocket callbacks
class MyWebsocket
  def initialize(env)
    # we need to change the encoding to UTF-8 of all transfers will be binary "blob" for the Javascript
    @nickname = env['PATH_INFO'][1..-1].force_encoding 'UTF-8'
  end

  def on_open
    puts 'We have a websocket connection'
  end

  def on_close
    puts "Bye Bye... only #{count} left..."
  end

  def on_shutdown
    puts "I'm shutting down #{self}"
  end

  def on_message(data)
    puts "got message: #{data} encoded as #{data.encoding}"
    # data is a temporary string, it's buffer cleared as soon as we return.
    # So we make a copy with the desired format.
    tmp = "#{@nickname}: #{data}"
    # The `write` method was added by the server and writes to the current connection
    write tmp
    puts "broadcasting #{tmp.bytesize} bytes with encoding #{tmp.encoding}"
    # `each` was added by the server and excludes this connection (each except self).
    each { |h| h.write tmp }
  end
end

run MyRackApplication
