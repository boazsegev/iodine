require 'socket'

require 'iodine/version'
require 'iodine/iodine'

# Iodine is an HTTP / WebSocket server as well as an Evented Network Tool Library.
#
# Here is a sample Echo server using Iodine:
#
#
#       # define the protocol for our service
#       class EchoProtocol
#         @timeout = 10
#         # this is just one possible callback with a recyclable buffer
#         def on_message buffer
#           # write the data we received
#           write "echo: #{buffer}"
#           # close the connection when the time comes
#           close if buffer =~ /^bye[\n\r]/
#         end
#       end
#       # create the service instance
#       Iodine.listen 3000, EchoProtocol
#       # start the service
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
# Please read the {file:README.md} file for an introduction to Iodine and an overview of it's API.
#
module Iodine
end


# require 'rack/handler/iodine' unless defined? ::Iodine::Rack::IODINE_RACK_LOADED
