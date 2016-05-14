#
#
class WSEcho
  def self.call env
    if env["HTTP_UPGRADE".freeze] =~ /websocket/i.freeze
      env['iodine.websocket'.freeze] = WSEcho
      return [0,{}, []]
    end
    out = "ENV:\r\n#{env.to_a.map {|h| "#{h[0]}: #{h[1]}"} .join "\n"}"
    request = Rack::Request.new(env)
    out += "\nRequest Path: #{request.path_info}\nParams:\r\n#{request.params.to_a.map {|h| "#{h[0]}: #{h[1]}"} .join "\n"}" unless(request.params.empty?)
    [200, {"Content-Length" => out.length.to_s}, [out]];
  end
  # def on_open
  #   puts "We have a websocket connection"
  # end
  # def on_close
  #   puts "Bye Bye... only #{ws_count} left..."
  # end
  def on_shutdown
    puts "I'm shutting down #{self}"
  end
  def on_message data
    # puts data
    # write data
    data = data.dup
    each {|h| h.echo data }
  end
  def echo data
    write "echo: #{data}"
  end
end

# puts "press Enter to start #{Process.pid}"
# gets
# Iodine::Rack.threads ||= 4

# run WSEcho

rack_app = Proc.new do |env|
	[200, {"Content-Type" => "text/html", "Content-Length" => "16"}, ['Hello from Rack!'] ]
	# [200, {"Content-Type" => "text/html"}, ['Hello from Rack!'] ]
end
run rack_app
