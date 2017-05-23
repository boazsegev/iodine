require 'benchmark'
require 'rack/sendfile'
require 'rack/lint'

# There are a number of possible applications to run in this file,
# because I use it to test stuff.
#
# This value (app) sets which of the different applications will run.
#
# Valid values are "hello", "slow" (debugs env values), "simple"
app = 'slow'

# This is a simple Hello World Rack application, for benchmarking.
HELLO_RESPONSE = [200, { 'Content-Type'.freeze => 'text/html'.freeze,
        'Content-Length'.freeze => '16'.freeze }.freeze,
 ['Hello from Rack!'.freeze]].freeze

hello = proc do |_env|
  HELLO_RESPONSE
end

slow = proc do |env|
  request = Rack::Request.new(env)
  # Benchmark.bm do |bm|
  #   bm.report('Reading from env Hash to a string X 1000') { 1000.times { out = "ENV:\r\n#{env.to_a.map { |h| "#{h[0]}: #{h[1]}" } .join "\n"}\n" } }
  #   bm.report('Creating the Rack::Request (can\'t repeat)') { 1.times { request = Rack::Request.new(env) } }
  # end
  if request.path_info == '/source'.freeze
    [200, { 'X-Sendfile' => File.expand_path(__FILE__) }, []]
  elsif request.path_info == '/file'.freeze
    [200, { 'X-Header' => 'This was a Rack::Sendfile response' }, File.open(__FILE__)]
  else
    out = "ENV:\n<br/>\n#{env.to_a.map { |h| "#{h[0]}: #{h[1]}" } .join "\n<br/>\n"}\n<br/>\n"
    out += "\n<br/>\nRequest Path: #{request.path_info}\n<br/>\nParams:\n<br/>\n#{request.params.to_a.map { |h| "#{h[0]}: #{h[1]}" } .join "\n<br/>\n"}\n<br/>\n" unless request.params.empty?
    [200, { 'Content-Type'.freeze => 'text/html'.freeze,
            'Content-Length'.freeze => out.length.to_s },
     [out]]
 end
end

simple = proc do |env|
  request = Rack::Request.new(env)
  if request.path_info == '/source'.freeze
    [200, { 'X-Sendfile' => File.expand_path(__FILE__) }, []]
  elsif request.path_info == '/file'.freeze
    [200, { 'X-Header' => 'This was a Rack::Sendfile response' }, File.open(__FILE__)]
  else
    [200, { 'Content-Type'.freeze => 'text/html'.freeze,
            'Content-Length'.freeze => request.path_info.length.to_s },
     [request.path_info]]
 end
end

logo_png = nil

big = proc do |_env|
  logo_png ||= IO.binread '../logo.png'
  [200, { 'Content-Length'.freeze => logo_png.length.to_s , 'Content-Type'.freeze => 'image/png'.freeze}, [logo_png]]
end

bigX = proc do |_env|
  logo_png ||= IO.binread '../logo.png'
  [200, { 'Content-Length'.freeze => logo_png.length.to_s , 'Content-Type'.freeze => 'image/png'.freeze, 'X-Sendfile'.freeze => '../logo.png'.freeze}, [logo_png]]
end

case app
when 'simple'
  use Rack::Sendfile
  run simple
when 'hello'
  run hello
when 'big'
  run big
when 'bigX'
  run bigX
when 'slow'
  use Rack::Lint
  run slow
else
  run hello
end

# ab -n 1000000 -c 2000 -k http://127.0.0.1:3000/
# wrk -c400 -d5 -t12 http://localhost:3000/
#
# def cycle
#   puts `wrk -c4000 -d5 -t12 http://localhost:3000/`
#   sleep(2)
#   puts `wrk -c4000 -d5 -t12 http://localhost:3000/source`
#   sleep(3)
#   puts `wrk -c200 -d5 -t12 http://localhost:3000/file`
#    true
# end
# sleep(10) while cycle
