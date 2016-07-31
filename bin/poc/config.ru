# The Rack Application container
module MyRackApplication
  # Rack applications use the `call` callback to handle HTTP requests.
  def self.call(env)
    # if upgrading...
    if env['HTTP_UPGRADE'.freeze] =~ /websocket/i
      # We can assign a class or an instance that implements callbacks.
      # We will assign an object, passing it the request information (`env`)
      env['iodine.websocket'.freeze] = MyWebsocket.new(env)
      # Rack responses must be a 3 item array
      # [status, {http: :headers}, ["response body"]]
      return [0, {}, []]
    end
    # a semi-regualr HTTP response
    out = File.open File.expand_path('../www/index.html', __FILE__)
    [200, { 'X-Sendfile' => File.expand_path('../www/index.html', __FILE__),
            'Content-Length' => out.size }, out]
  end
end

# The Websocket Callback Object
class MyWebsocket
  # this is optional, but I wanted the object to have the nickname provided in
  # the HTTP request
  def initialize(env)
    # we need to change the ASCI Rack encoding to UTF-8,
    # otherwise everything with the nickname will be a binary "blob" in the
    # Javascript layer
    @nickname = env['PATH_INFO'][1..-1].force_encoding 'UTF-8'
  end

  # A classic websocket callback, called when the connection is opened and
  # linked to this object
  def on_open
    puts 'We have a websocket connection'
  end

  # A classic websocket callback, called when the connection is closed
  # (after disconnection).
  def on_close
    puts "Bye Bye... #{count} connections left..."
  end

  # A server-side niceness, called when the server if shutting down,
  # to gracefully disconnect (before disconnection).
  def on_shutdown
    write 'The server is shutting down, goodbye.'
  end

  def on_message(data)
    puts "got message: #{data} encoded as #{data.encoding}"
    # data is a temporary string, it's buffer cleared as soon as we return.
    # So we make a copy with the desired format.
    tmp = "#{@nickname}: #{data}"
    # The `write` method was added by the server and writes to the current
    # connection
    write tmp
    puts "broadcasting #{tmp.bytesize} bytes with encoding #{tmp.encoding}"
    # `each` was added by the server and excludes this connection
    # (each except self).
    each { |h| h.write tmp }
  end
end

# `run` is a Rack API command, telling Rack where the `call(env)` callback is located.
run MyRackApplication
