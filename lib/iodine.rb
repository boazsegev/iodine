require 'socket'

require 'iodine/version'
require 'iodine/iodine'

# Iodine is an HTTP / WebSocket server as well as an Evented Network Tool Library.
#
# Here is a sample Echo server using Iodine:
#
#
#       require 'iodine'
#       # define the protocol for our service
#       class EchoProtocol
#         def on_open(client)
#           # Set a connection timeout
#           client.timeout = 10
#           # Write a welcome message
#           client.write "Echo server running on Iodine #{Iodine::VERSION}.\r\n"
#         end
#         # this is called for incoming data - note data might be fragmented.
#         def on_message(client, data)
#           # write the data we received
#           client.write "echo: #{data}"
#           # close the connection when the time comes
#           client.close if data =~ /^bye[\n\r]/
#         end
#         # called if the connection is still open and the server is shutting down.
#         def on_shutdown(client)
#           # write the data we received
#           client.write "Server going away\r\n"
#         end
#       end
#       # create the service instance, the block returns a connection handler.
#       Iodine.listen(port: "3000") { EchoProtocol.new }
#       # start the service
#       Iodine.threads = 1
#       Iodine.start
#
#
#
# Methods for setting up and starting {Iodine} include {start}, {threads}, {threads=}, {workers} and {workers=}.
#
# Methods for setting startup / operational callbacks include {on_idle}, {before_fork} and {after_fork}.
#
# Methods for asynchronous execution include {run} (same as {defer}), {run_after} and {run_every}.
#
# Methods for application wide pub/sub include {subscribe}, {unsubscribe} and {publish}. Connection specific pub/sub methods are documented in the {Iodine::Connection} class).
#
# Methods for TCP/IP and Unix Sockets connections include {listen} and {connect}.
#
# Methods for HTTP connections include {listen2http}.
#
# Note that the HTTP server supports both TCP/IP and Unix Sockets as well as SSE / WebSockets extensions.
#
# Please read the {file:README.md} file for an introduction to Iodine.
#
module Iodine

    # Will monkey patch some Rack methods to increase their performance.
    #
    # This is recommended, see {Iodine::Rack::Utils} for details.
    def self.patch_rack
    ::Rack::Utils.class_eval do
      Iodine::Base::MonkeyPatch::RackUtils.methods(false).each do |m|
        ::Rack::Utils.define_singleton_method(m,
              Iodine::Base::MonkeyPatch::RackUtils.instance_method(m) )
        end
      end
    end

end

require 'rack/handler/iodine' unless defined? ::Iodine::Rack::IODINE_RACK_LOADED


### CLI argument parsing

if ARGV.index('-b') && ARGV[ARGV.index('-b') + 1]
  Iodine::DEFAULT_HTTP_ARGS[:address] = ARGV[ARGV.index('-b') + 1]
end
if ARGV.index('-p') && ARGV[ARGV.index('-p') + 1]
  Iodine::DEFAULT_HTTP_ARGS[:port] = ARGV[ARGV.index('-p') + 1]
end

if ARGV.index('-maxbd') && ARGV[ARGV.index('-maxbd') + 1]
  Iodine::DEFAULT_HTTP_ARGS[:max_body_size] = ARGV[ARGV.index('-maxbd') + 1].to_i
end
if ARGV.index('-maxms') && ARGV[ARGV.index('-maxms') + 1]
  Iodine::DEFAULT_HTTP_ARGS[:max_msg_size] = ARGV[ARGV.index('-maxms') + 1].to_i
end
if ARGV.index('-maxhead') && ARGV[ARGV.index('-maxhead') + 1]  && ARGV[ARGV.index('-maxhead') + 1].to_i > 0
  Iodine::DEFAULT_HTTP_ARGS[:max_headers] = ARGV[ARGV.index('-maxhead') + 1].to_i
end
if ARGV.index('-ping') && ARGV[ARGV.index('-ping') + 1]
  Iodine::DEFAULT_HTTP_ARGS[:ping] = ARGV[ARGV.index('-ping') + 1].to_i
end
if ARGV.index('-www') && ARGV[ARGV.index('-www') + 1]
  Iodine::DEFAULT_HTTP_ARGS[:public] = ARGV[ARGV.index('-www') + 1]
end
if ARGV.index('-tout') && ARGV[ARGV.index('-tout') + 1]
  Iodine::DEFAULT_HTTP_ARGS[:timeout] = ARGV[ARGV.index('-tout') + 1].to_i
  puts "WARNNING: timeout set to 0 (ignored, timeout will be ~5 seconds)." if (Iodine::DEFAULT_HTTP_ARGS[:timeout].to_i <= 0 || Iodine::DEFAULT_HTTP_ARGS[:timeout].to_i > 255)
end
Iodine::DEFAULT_HTTP_ARGS[:log] = true if ARGV.index('-v')
Iodine::DEFAULT_HTTP_ARGS[:log] = false if ARGV.index('-q')


