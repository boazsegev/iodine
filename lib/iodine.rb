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
module Iodine
  @threads = (ARGV.index('-t') && ARGV[ARGV.index('-t') + 1]) || ENV['MAX_THREADS']
  @processes = (ARGV.index('-w') && ARGV[ARGV.index('-w') + 1]) || ENV['MAX_WORKERS']
  @threads = @threads.to_i if @threads
  if @processes == 'auto'
    begin
      require 'etc'
      @processes = Etc.nprocessors
    rescue Exception
      warn "This version of Ruby doesn't support automatic CPU core detection."
    end
  end
  @processes = @processes.to_i if @processes

  # Get/Set the number of threads used in the thread pool (a static thread pool). Can be 1 (single working thread, the main thread will sleep) and can be 0 (the main thread will be used as the only active thread).
  def self.threads
    @threads
  end

  # Get/Set the number of threads used in the thread pool (a static thread pool). Can be 1 (single working thread, the main thread will sleep) and can be 0 (the main thread will be used as the only active thread).
  def self.threads=(count)
    @threads = count.to_i
  end

  # Get/Set the number of worker processes. A value greater then 1 will cause the Iodine to "fork" any extra worker processes needed.
  def self.processes
    @processes
  end

  # Get/Set the number of worker processes. A value greater then 1 will cause the Iodine to "fork" any extra worker processes needed.
  def self.processes=(count)
    @processes = count.to_i
  end

  # Runs the warmup sequence. {warmup} attempts to get every "autoloaded" (lazy loaded)
  # file to load now instead of waiting for "first access". This allows multi-threaded safety and better memory utilization during forking.
  #
  # Use {warmup} when either {processes} or {threads} are set to more then 1.
  def self.warmup app
    # load anything marked with `autoload`, since autoload isn't thread safe nor fork friendly.
    Module.constants.each do |n|
      begin
        Object.const_get(n)
      rescue Exception => _e
      end
    end
    ::Rack::Builder.new(app) do |r|
      r.warmup do |app|
        client = ::Rack::MockRequest.new(app)
        client.get('/')
      end
    end
  end
end

require 'rack/handler/iodine' unless defined? ::Iodine::Rack::IODINE_RACK_LOADED
