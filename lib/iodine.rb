require "socket"

require "iodine/version"
require "iodine/logging"
require "iodine/iodine"

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
class Iodine
  # The {#initialize} method accepts either a Hash of settings or no arguments (default state).
  #
  # These are the settings:
  # protocol:: a class will be used as the initial protocol for every connection. Defaults to `nil`.
  # port:: the port to listen to. defaults to the port stated by the runtime flag (ARGV) " -p ####" or, if missing, `"3000"`.
  # address:: the address to bind to (if not all localhost addresses). Setting this flag might prevent IPv4 or IPv6 addresses from being utilized. It is best not to avoid setting an address. Defaults to `nil`.
  # threads:: the number of worker threads that will handle protocol tasks (except `on_open`, `on_close` and `ping` that are handled by the reactor's thread). Defaults to 1. Increase this if your server might have blocking or long running code.
  # processes:: the number of processes (forks). A value of 1 means that the server will run a using single process (the process invoking `Iodine#start`). Defaults to 1.
  # timeout:: the default timeout in approximated seconds (fuzzy timeout) for net connections. Defaults to 10 (that's long, it's recommended to lower this value).
  # busy_msg:: when the server is busy, this optional String (no NULL characters allowed) will be sent before refusing new connection requests.
  #
  # All settings can be set using a getter and a setter as well. i.e.
  #
  #      server = Iodine.new
  #      server.timeout = 3
  #
  # Once the settings were set, start the server using:
  #
  #      server.start
  #
  # To run concurrent tasks, use the Protocol's API once it was initialized (during the `on_open` callback and not during `initialize`).
  #
  def initialize state = {}
    if !state.is_a?(Hash)
      raise TypeError, "The `new` method accepts either a Hash with initial settings or no parameters."
    end
    @protocol = state[:protocol]
    @port = (state[:port] || (ARGV.index('-p') && ARGV[ARGV.index('-p') + 1]) || ENV['PORT'] || "3000").to_i
    @threads = (state[:threads] || (ARGV.index('-t') && ARGV[ARGV.index('-t') + 1]) || ENV['MAX_THREADS'])
    @threads = @threads.to_i if @threads
    @processes = (state[:processes] || (ARGV.index('-w') && ARGV[ARGV.index('-w') + 1]) || ENV['MAX_WORKERS'])
    @processes = @processes.to_i if @processes
    @timeout = state[:timeout]
    @busy_msg = state[:busy_msg]
  end
end

require "rack/handler/iodine"
