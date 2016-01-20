# Iodine is a platform for writing evented network services on Ruby.
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
class Iodine
  # This method accepts a block and sets the block to run immediately after the server had started.
  #
  # Since functions such as `run`, `run_every` and `run_after` can only be called AFTER the reactor had started,
  # use {#on_start} to setup any global tasks. i.e.
  #
  #     server = Iodine.new
  #     server.on_start do
  #         puts "My network service had started."
  #         server.run_every(5000) {puts "#{server.count -1 } clients connected."}
  #     end
  #     # ...
  #
  def on_start
  end
  # starts the service.
  def start
  end
  # Runs an asynchronous task.
  #
  # This method can only be called while the service is running. Use this method within the {#on_start} initialization block.
  def run
  end
  # Runs an asynchronous task after set amount of milliseconds.
  #
  # This method can only be called while the service is running. Use this method within the {#on_start} initialization block.
  def run_after milliseconds
  end
  # Runs an asynchronous task every set amount of milliseconds. see {DynamicProtocol#run_every}.
  #
  # This method can only be called while the service is running. Use this method within the {#on_start} initialization block.
  def run_every milliseconds
  end
  # returns the total number of connections and timers currently used.
  # Timers ARE connections in their implementation and aren't counted seperately by Iodine.
  #
  # This method can only be called while the service is running. Use this method within the {#on_start} initialization block.
  def connection_count
  end
end
