
rack_app = Proc.new do |env|
	# [200, {"Content-Type" => "text/html", "Content-Length" => "16"}, ['Hello from Rack!'] ]
	[200, {"Content-Type" => "text/html", "Content-Length" => "16", "connection" => "close"}, ['Hello from Rack!'] ]
end

run rack_app
