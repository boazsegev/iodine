require 'socket'

require 'iodine/version'
require 'iodine/iodine'

# Iodine is both a Rack server and a platform for writing evented network services on Ruby.
#
# Here is a sample Echo server using Iodine:
#
#
#       # define the protocol for our service
#       class EchoProtocol
#         # this is just one possible callback with a recyclable buffer
#         def on_message buffer
#           # write the data we received
#           write "echo: #{buffer}"
#           # close the connection when the time comes
#           close if buffer =~ /^bye[\n\r]/
#         end
#       end
#       # create the service instance
#       echo_server = Iodine.new timeout:10,
#                                protocol: EchoProtocol
#       # start the service
#       echo_server.start
#
#
# Please read the {file:README.md} file for an introduction to Iodine and an overview of it's API.
module Iodine
  @threads = (ARGV.index('-t') && ARGV[ARGV.index('-t') + 1]) || ENV['MAX_THREADS']
  @processes = (ARGV.index('-w') && ARGV[ARGV.index('-w') + 1]) || ENV['MAX_WORKERS']
  @threads = @threads.to_i if @threads
  @processes = @processes.to_i if @processes

  def self.threads
    @threads
  end

  def self.threads=(count)
    @threads = count.to_i
  end

  def self.processes
    @processes
  end

  def self.processes=(count)
    @processes = count.to_i
  end
end

require 'rack/handler/iodine'
