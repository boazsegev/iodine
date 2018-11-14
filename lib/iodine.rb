require 'socket'

require 'iodine/version'
require 'iodine/iodine'

# Iodine is an HTTP / WebSocket server as well as an Evented Network Tool Library. In essense, Iodine is a Ruby port for the [facil.io](http://facil.io) C library.
#
# Here is a simple telnet based echo server using Iodine (see full list at {Iodine::Connection}):
#
#
#       require 'iodine'
#       # define the protocol for our service
#       module EchoProtocol
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
#         extend self
#       end
#       # create the service instance, the block returns a connection handler.
#       Iodine.listen(port: "3000") { EchoProtocol }
#       # start the service
#       Iodine.threads = 1
#       Iodine.start
#
#
#
# Methods for setting up and starting {Iodine} include {start}, {threads}, {threads=}, {workers} and {workers=}.
#
# Methods for setting startup / operational callbacks include {on_idle}, {on_state}.
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
# Iodine doesn't call {patch_rack} automatically, but doing so will improve Rack's performace.
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


    # @deprecated use {Iodine.on_state}.
    #
    # Sets a block of code to run before a new worker process is forked (cluster mode only).
    def self.before_fork(&block)
      # warn "Iodine.before_fork is deprecated, use Iodine.on_state(:before_fork)."
      Iodine.on_state(:before_fork, &block)
    end
    # @deprecated use {Iodine.on_state}.
    #
    # Sets a block of code to run after a new worker process is forked (cluster mode only).
    #
    # Code runs in both the parent and the child.
    def self.after_fork(&block)
      # warn "Iodine.after_fork is deprecated, use Iodine.on_state(:after_fork)."
      Iodine.on_state(:after_fork, &block)
    end
    # @deprecated use {Iodine.on_state}.
    #
    # Sets a block of code to run in the worker process, after a new worker process is forked (cluster mode only).
    def self.after_fork_in_worker(&block)
      warn "Iodine.after_fork_in_worker is deprecated, use Iodine.on_state(:enter_child)."
      Iodine.on_state(:enter_child, &block)
    end
    # @deprecated use {Iodine.on_state}.
    #
    # Sets a block of code to run in the master / root process, after a new worker process is forked (cluster mode only).
    def self.after_fork_in_master(&block)
      warn "Iodine.after_fork_in_master is deprecated, use Iodine.on_state(:enter_master)."
      Iodine.on_state(:enter_master, &block)
    end
    # @deprecated use {Iodine.on_state}.
    #
    # Sets a block of code to run once a Worker process shuts down (both in single process mode and cluster mode).
    def self.on_shutdown(&block)
      warn "Iodine.on_shutdown is deprecated, use Iodine.on_state(:on_finish)."
      Iodine.on_state(:on_finish, &block)
    end

end

require 'rack/handler/iodine' unless defined? ::Iodine::Rack::IODINE_RACK_LOADED


### Automatic ActiveRecord fix
Iodine.on_state(:before_fork)  do
  if defined?(ActiveRecord) && defined?(ActiveRecord::Base) && ActiveRecord::Base.respond_to?(:connection)
    begin
      ActiveRecord::Base.connection.disconnect!
    rescue
    end
  end
end
Iodine.on_state(:after_fork)  do
  if defined?(ActiveRecord) && defined?(ActiveRecord::Base) && ActiveRecord::Base.respond_to?(:establish_connection)
    begin
      ActiveRecord::Base.establish_connection
    rescue
    end
  end
end

### Old CLI argument parsing - TODO: remove old code

# if ARGV.index('-b') && ARGV[ARGV.index('-b') + 1]
#   Iodine::DEFAULT_HTTP_ARGS[:address] = ARGV[ARGV.index('-b') + 1]
# end
# if ARGV.index('-p') && ARGV[ARGV.index('-p') + 1]
#   Iodine::DEFAULT_HTTP_ARGS[:port] = ARGV[ARGV.index('-p') + 1]
# end

# if ARGV.index('-maxbd') && ARGV[ARGV.index('-maxbd') + 1]
#   Iodine::DEFAULT_HTTP_ARGS[:max_body_size] = ARGV[ARGV.index('-maxbd') + 1].to_i
# end
# if ARGV.index('-maxms') && ARGV[ARGV.index('-maxms') + 1]
#   Iodine::DEFAULT_HTTP_ARGS[:max_msg_size] = ARGV[ARGV.index('-maxms') + 1].to_i
# end
# if ARGV.index('-maxhead') && ARGV[ARGV.index('-maxhead') + 1]  && ARGV[ARGV.index('-maxhead') + 1].to_i > 0
#   Iodine::DEFAULT_HTTP_ARGS[:max_headers] = ARGV[ARGV.index('-maxhead') + 1].to_i
# end
# if ARGV.index('-ping') && ARGV[ARGV.index('-ping') + 1]
#   Iodine::DEFAULT_HTTP_ARGS[:ping] = ARGV[ARGV.index('-ping') + 1].to_i
# end
# if ARGV.index('-www') && ARGV[ARGV.index('-www') + 1]
#   Iodine::DEFAULT_HTTP_ARGS[:public] = ARGV[ARGV.index('-www') + 1]
# end
# if ARGV.index('-tout') && ARGV[ARGV.index('-tout') + 1]
#   Iodine::DEFAULT_HTTP_ARGS[:timeout] = ARGV[ARGV.index('-tout') + 1].to_i
#   puts "WARNNING: timeout set to 0 (ignored, timeout will be ~5 seconds)." if (Iodine::DEFAULT_HTTP_ARGS[:timeout].to_i <= 0 || Iodine::DEFAULT_HTTP_ARGS[:timeout].to_i > 255)
# end

# Iodine::DEFAULT_HTTP_ARGS[:log] = true if ARGV.index('-v')

# if ARGV.index('-logging') && ARGV[ARGV.index('-logging') + 1] && ARGV[ARGV.index('-logging') + 1].to_s[0] >= '0' && ARGV[ARGV.index('-logging') + 1].to_s[0] <= '9'
#   Iodine.verbosity = ARGV[ARGV.index('-logging') + 1].to_i
# end

# if ARGV.index('-t') && ARGV[ARGV.index('-t') + 1].to_i != 0
#   Iodine.threads = ARGV[ARGV.index('-t') + 1].to_i
# end
# if ARGV.index('-w') && ARGV[ARGV.index('-w') + 1].to_i != 0
#   Iodine.workers = ARGV[ARGV.index('-w') + 1].to_i
# end

### Old CLI converter

ARGV[ARGV.index('-www')] = "--www" if ARGV.index('-www')
ARGV[ARGV.index('-maxbd')] = "--maxbd" if ARGV.index('-maxbd')
ARGV[ARGV.index('-maxms')] = "--maxms" if ARGV.index('-maxms')
ARGV[ARGV.index('-maxhead')] = "--maxhead" if ARGV.index('-maxhead')
ARGV[ARGV.index('-ping')] = "--ping" if ARGV.index('-ping')
ARGV[ARGV.index('-tout')] = "--tout" if ARGV.index('-tout')
ARGV[ARGV.index('-logging')] = "--logging" if ARGV.index('-logging')

### CLI argument parsing

require 'optparse'

module Iodine
  # The Iodine::Base namespace is reserved for internal use and is NOT part of the public API.
  module Base
    # Command line interface. The Ruby CLI might be changed in future versions.
    module CLI
      # initializes the CLI parser.
      def self.cli_parser
        @opt ||= OptionParser.new do |opts|
          opts.banner = "Iodine's HTTP/WebSocket server version #{Iodine::VERSION}\r\n\r\nUse:\r\n    iodine <options> <filename>\r\n\r\nBoth <options> and <filename> are optional. i.e.,:\r\n    iodine -p 0 -b /tmp/my_unix_sock\r\n    iodine -p 8080 path/to/app/conf.ru\r\n    iodine -p 8080 -w 4 -t 16\r\n"
          opts.on("-w", "--workers N", Integer, "Number of threads per worker. Default: machine specific.") do |o|
            Iodine.workers = o.to_i
          end
          opts.on("-t", "--threads N", Integer, "Number of worker processes (using fork). Default: machine specific.") do |o|
            Iodine.threads = o.to_i
          end
          opts.on("-p", "--port PORT", Integer, "HTTP to listen to (0 => Unix Socket). Defaults to 3000") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:port] = o.to_s
          end
          opts.on("-b", "--address ADDR", String, "HTTP address to listen to (IP / Unix Socket). Defaults to nil (all).") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:address] = o.to_s
          end
          opts.on("--www FOLDER", "--public FOLDER", String, "Public folder for static file serving. Default: nil (none).") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:public] = o.to_s
          end
          opts.on("-v", "--log", "Log HTTP requests to STDERR.") do
            Iodine::DEFAULT_HTTP_ARGS[:log] = true
          end
          opts.on("--maxbd MB", Integer, "Maximum MegaBytes per HTTP message (max body size). Default: 50Mb.") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:max_body_size] = o.to_i * 1024 * 1024
          end
          opts.on("--maxhead BYTES", Integer, "Maximum total headers length per HTTP request. Default: 32Kb.") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:max_headers] = o.to_i
          end
          opts.on("--tout SEC", Integer, "HTTP inactivity connection timeout. Default: 40 seconds.") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:timeout] = o.to_i
          end
          opts.on("--maxms BYTES", Integer, "Maximum Bytes per WebSocket message. Default: 250Kb.") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:max_msg_size] = o.to_i
          end
          opts.on("--ping SEC", Integer, "WebSocket / SSE ping interval in seconds. Default: 40 seconds.") do |o|
            Iodine::DEFAULT_HTTP_ARGS[:ping] = o.to_i
          end
          opts.on("-V", "--logging LEVEL", Integer, "Server level logging (not HTTP), values between 0..5. Defaults to 4.") do |o|
            Iodine.verbosity = o.to_i
          end
        end
      end

      # Tests for the -?, -h or --help arguments
      def self.cli_test4help
        opt = cli_parser
        opt.on_tail("-?", "-h", "--help", "Prints this help") do
          puts opt
          exit
        end
        opt
      end
    end
  end
end

Iodine::Base::CLI.cli_parser.parse

### Puma / Thin DSL compatibility - depracated (DSLs are evil)

if(!defined?(after_fork))
  # @deprecated use {Iodine.on_state}.
  #
  # Performs a block of code whenever a new worker process spins up (performed once per worker).
  def after_fork(*args, &block)
    warn "after_fork is deprecated, use Iodine.on_state(:after_fork)."
    Iodine.on_state(:after_fork, &block)
  end
end
if(!defined?(after_fork_in_worker))
  # @deprecated use {Iodine.on_state}.
  #
  # Performs a block of code whenever a new worker process spins up (performed once per worker).
  def after_fork_in_worker(*args, &block)
    warn "after_fork_in_worker is deprecated, use Iodine.on_state(:enter_child)."
    Iodine.on_state(:enter_child, &block)
  end
end
if(!defined?(after_fork_in_master))
  # @deprecated use {Iodine.on_state}.
  #
  # Performs a block of code whenever a new worker process spins up (performed once per worker).
  def after_fork_in_master(*args, &block)
    warn "after_fork_in_master is deprecated, use Iodine.on_state(:enter_master)."
    Iodine.on_state(:enter_master, &block)
  end
end
if(!defined?(on_worker_boot))
  # @deprecated use {Iodine.on_state}.
  #
  # Performs a block of code before a new worker process spins up (performed once per worker).
  def on_worker_boot(*args, &block)
    warn "on_worker_boot is deprecated, use Iodine.on_state(:after_fork)."
    Iodine.on_state(:after_fork, &block)
  end
end
if(!defined?(on_worker_fork))
  # @deprecated use {Iodine.on_state}.
  #
  # Performs a block of code before a new worker process spins up (performed once per worker).
  def on_worker_fork(*args, &block)
    warn "on_worker_fork is deprecated, use Iodine.on_state(:before_fork)."
    Iodine.on_state(:before_fork, &block)
  end
end
if(!defined?(before_fork))
  # @deprecated use {Iodine.on_state}.
  #
  # Performs a block of code just before a new worker process spins up (performed once per worker, in the master thread).
  def before_fork(*args, &block)
    warn "before_fork is deprecated, use Iodine.on_state(:before_fork)."
    Iodine.on_state(:before_fork, &block)
  end
end





