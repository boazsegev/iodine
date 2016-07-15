require 'benchmark'
# This is a simple Hello World Rack application, for benchmarking.
rack_app = proc do |_env|
  [200, { 'Content-Type'.freeze => 'text/html'.freeze,
          'Content-Length'.freeze => '16'.freeze },
   ['Hello from Rack!'.freeze]]
end

rack_app_slower = proc do |env|
  out = nil
  request = nil
  Benchmark.bm do |bm|
    bm.report('Reading from env Hash to a string X 1000') { 1000.times { out = "ENV:\r\n#{env.to_a.map { |h| "#{h[0]}: #{h[1]}" } .join "\n"}\n" } }
    bm.report('Creating the Rack::Request (can\'t repeat)') { 1.times { request = Rack::Request.new(env) } }
  end

  out += "\nRequest Path: #{request.path_info}\nParams:\r\n#{request.params.to_a.map { |h| "#{h[0]}: #{h[1]}" } .join "\n"}\n" unless request.params.empty?
  [200, { 'Content-Type'.freeze => 'text/html'.freeze,
          'Content-Length'.freeze => out.length.to_s },
   [out]]
end

run rack_app
