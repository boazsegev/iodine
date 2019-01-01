require 'socket'  # TCPSocket is used internally for Hijack support
# require 'openssl' # For SSL/TLS support using OpenSSL

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
      begin
        require 'rack'
      rescue LoadError
      end
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
      warn "Iodine.before_fork is deprecated, use Iodine.on_state(:before_fork)."
      Iodine.on_state(:before_fork, &block)
    end
    # @deprecated use {Iodine.on_state}.
    #
    # Sets a block of code to run after a new worker process is forked (cluster mode only).
    #
    # Code runs in both the parent and the child.
    def self.after_fork(&block)
      warn "Iodine.after_fork is deprecated, use Iodine.on_state(:after_fork)."
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

    module PubSub
      # @deprecated use {Iodine::PubSub.detach}.
      def self.dettach(engine)
        warn "Iodine::PubSub.dettach is deprecated (was a typo), use Iodine::PubSub.detach(engine)."
        Iodine::PubSub.detach(engine)
      end
    end

end

require 'rack/handler/iodine' unless defined? ::Iodine::Rack::IODINE_RACK_LOADED


### Automatic ActiveRecord and Sequel support for forking (preventing connection sharing)
Iodine.on_state(:before_fork)  do
  if defined?(ActiveRecord) && defined?(ActiveRecord::Base) && ActiveRecord::Base.respond_to?(:connection)
    begin
      ActiveRecord::Base.connection.disconnect!
    rescue
    end
  end
  if defined?(Sequel)
    begin
      Sequel::DATABASES.each { |database| database.disconnect }
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

### Parse CLI for default HTTP settings
Iodine::Base::CLI.parse

### Initialize Redis if set in CLI
Iodine::PubSub.default = Iodine::PubSub::Redis.new(Iodine::DEFAULT_HTTP_ARGS[:redis_], ping: Iodine::DEFAULT_HTTP_ARGS[:redis_ping_]) if Iodine::DEFAULT_HTTP_ARGS[:redis_]

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





