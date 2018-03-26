# This is a simple Hello World Rack application
#
# Running this application from the command line is eacy with:
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
  RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '12' }, [ 'Hello World!' ] ]

   # this is function will be called by the Rack server (iodine) for every request.
   def self.call env
     # simply return the RESPONSE object, no matter what request was received.
     RESPONSE
   end
end

# this function call links our HelloWorld application with Rack
run HelloWorld
