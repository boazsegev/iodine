# This is a simple Hello World Rack application
#
# Running this application from the command line is easy with:
#
#      iodine hello.ru
#
# Or, in single thread and single process:
#
#      iodine -t 1 -w 1 hello.ru
#
# Benchmark with `ab` or `wrk` (a 5 seconds benchmark with 2000 concurrent clients):
#
#      ab -c 2000 -t 5 -n 1000000 -k http://127.0.0.1:3000/
#      wrk -c2000 -d5 -t12 http://localhost:3000/

module HelloWorld
  # This is the HTTP response object according to the Rack specification.

  # this is function will be called by the Rack server (iodine) for every request.
  def self.call env
    req = Rack::Request.new(env)

    headers = Rack::Headers.new

    if req.params['last_modified'] == 'foo'
      headers['Last-Modified'] = req.params['last_modified']
    elsif req.params['last_modified'] == 'nil'
      headers['Last-Modified'] = nil
    elsif req.params['last_modified']
      headers['Last-Modified'] = Time.now.httpdate
    end
    [ 200, headers, [] ]
  end
end

# this function call links our HelloWorld application with Rack
run HelloWorld
