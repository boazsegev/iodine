# This is a simple Hello World Rack application, for benchmarking.
rack_app = Proc.new do |env|
	[200, {"Content-Type".freeze => "text/html".freeze,
    "Content-Length".freeze => "16".freeze},
    ['Hello from Rack!'.freeze] ]
end
run rack_app
