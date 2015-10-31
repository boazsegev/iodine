require 'logger'
require 'socket'
require 'openssl'

require "iodine/version"
require "iodine/settings"
require "iodine/logging"
require "iodine/core"
require "iodine/timers"
require "iodine/protocol"
require "iodine/ssl_connector"
require "iodine/io"
require "iodine/core_init"



# Iodine is an easy Object-Oriented library for writing network applications (servers) with your own
# network protocol.
#
# Please read the {file:README.md} file for an introduction to Iodine.
#
# Here's a quick and easy echo server,
# notice how Iodine will automatically start running once you finish setting everything up:
#
#       require 'iodine'
#
#       class MyProtocol < Iodine::Protocol
#          # Iodine will call this whenever a new connection is opened.
#          def on_open
#             # Iodine includes logging as well as unique assigned instance ID's.
#             Iodine.info "New connection id: #{id}"
#             # Iodine includes timeout support with automatic pinging or connection termination.
#             set_timeout 5
#          end
#          def on_message data
#             write("-- Closing connection, goodbye.\n") && close if data =~ /^(bye|close|exit)/i
#             write(">> #{data.chomp}\n")
#          end
#          # Iodine will call this whenever a new connection is closed.
#          def on_close
#             Iodine.info "Closing connection id: #{id}"
#          end
#          # Iodine will call this whenever a a timeout is reached.
#          def ping
#             # If `write` fails, it automatically closes the connection.
#             write("-- Are you still there?\n")
#          end
#       end
#
#       # setting up the server is as easy as plugging in your Protocol class:
#       Iodine.protocol = MyProtocol
#
#       # setting up cuncurrency can be done with threads - the default is a single thread):
#       Iodine.threads = 8
#
#       # setting up cuncurrency can also be done with processes (forking) - the default is a single process:
#       Iodine.processes = 4
#       
#       # if you are excecuting this script from IRB, exit IRB to start Iodine.
#       exit
#
# You can test the example using `telnet`
#
# Before you write your own protocol, make sure you learn more about the {Iodine::Protocol} and all it's got to offer.
module Iodine
  extend self
end



# require 'iodine/http'