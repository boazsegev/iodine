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
# Please read the {file:README.md} file for an introduction to Iodine and an overview of it's API.
#
module Iodine
end


# require 'rack/handler/iodine' unless defined? ::Iodine::Rack::IODINE_RACK_LOADED
