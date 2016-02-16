

rack_app= Proc.new do |env|
  out = "ENV:\r\n#{env.to_a.map {|h| "#{h[0]}: #{h[1]}"} .join "\n"}"
  request = Rack::Request.new(env)
  out += "\nRequest Path: #{request.path_info}\nParams:\r\n#{request.params.to_a.map {|h| "#{h[0]}: #{h[1]}"} .join "\n"}" unless(request.params.empty?)
  [200, {"Content-Length" => out.length}, [out]];
end


# rack_app = Proc.new do |env|
# 	[200, {"Content-Type" => "text/html", "Content-Length" => "16"}, ['Hello from Rack!'] ]
# 	# [200, {"Content-Type" => "text/html"}, ['Hello from Rack!'] ]
# end

run rack_app
