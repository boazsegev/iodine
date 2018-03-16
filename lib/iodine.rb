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
#
# == The API
#
# The main API methods for the top {Iodine} namesapce are grouped here by subject.
#
# === Event Loop / Concurrency
#
# Iodine manages an internal event-loop and reactor pattern. The following API
# manages Iodine's behavior.
#
# * {Iodine.threads}, {Iodine.threads=} gets or sets the amount of threads iodine will use in it's working thread pool.
# * {Iodine.processes}, {Iodine.processes} gets or sets the amount of processes iodine will utilize (`fork`) to handle connections.
# * {Iodine.start} starts iodine's event loop and reactor pattern. At this point, it's impossible to change the number of threads or processes used.
#
# === Event and Task Scheduling
#
# * {Iodine.run} schedules a block of code to run asynchronously.
# * {Iodine.run_after}, {Iodine.run_every} schedules a block of code to run (asynchronously) using a timer.
# * {Iodine.start} starts iodine's event loop and reactor pattern. At this point, it's impossible to change the number of threads or processes used.
#
# In addition to the top level API, there's also the connection class and connection instance API, as specified in the {Iodine::Protocol} and {Iodine::Websocket} documentation, which allows for a connection bound task(s) to be scheduled to run within the connection's lock (for example, {Iodine::Websocket#defer} and {Iodine::Websocket#each}).
#
# === Connection Handling
#
# Iodine handles connections using {Iodine::Protocol} objects. The following API
# manages either built-in or custom {Protocol} objects (classes / instances) in relation to their network sockets.
#
# * {Iodine.attach_fd}, {Iodine.attach_io} allows Iodine to take controll of an IO object (i.e., a TCP/IP Socket, a Unix Socket or a pipe).
# * {Iodine.connect} creates a new TCP/IP connection using the specified Protocol.
# * {Iodine.listen} listens to new TCP/IP connections using the specified Protocol.
# * {Iodine.listen2http} listens to new TCP/IP connections using the buildin HTTP / Websocket Protocol.
# * {Iodine.warmup} warms up any HTTP Rack applications.
# * {Iodine.count} counts the number of connections (including HTTP / Websocket connections).
# * {Iodine::Protocol.each} runs a code of block for every connection sharing the process (except HTTP / Websocket connections).
# * {Iodine::Websocket.each} runs a code of block for every existing websocket sharing the process.
#
# In addition to the top level API, there's also the connection class and connection instance API, as specified in the {Iodine::Protocol} and {Iodine::Websocket} documentation.
#
# === Pub/Sub
#
# Iodine offers a native Pub/Sub engine (no database required) that can be easily extended by implementing a Pub/Sub {Iodine::PubSub::Engine}.
#
# The following methods offect server side Pub/Sub that allows the server code to react to channel event.
#
# * {Iodine.subscribe}, {Iodine.unsubscribe} manages a process's subscription to a channel (which is different than a connection's subscription, such as employed by {Iodine::Websocket}).
# * {Iodine.publish} publishes a message to a Pub/Sub channel. The message will be sent to all subscribers - connections, other processes in the cluster and even other machines (when using the {Iodine::PubSub::RedisEngine}).
# * {Iodine.default_pubsub=}, {Iodine.default_pubsub} sets or gets the default Pub/Sub {Iodine::PubSub::Engine}. i.e., when set to a new {Iodine::PubSub::RedisEngine} instance, all Pub/Sub method calls will use the Redis engine (unless explicitly requiring a different engine).
#
# {Iodine::Websocket} objects have a seperate Pub/Sub implementation that manages the subscription's lifetime to match the connection's lifetime and allows direct client Pub/Sub (forwards the message to the client directly without invoking the Ruby interpreter).
#
# == Patching Rack
#
# Although Iodine offers Rack::Utils optimizations using monkey patching, Iodine does NOT monkey patch Rack automatically.
#
# Choosing to monkey patch Rack::Utils could offer significant performance gains for some applications. i.e. (on my machine):
#
#       require 'iodine'
#       require 'rack'
#       # a String in need of decoding
#       s = '%E3%83%AB%E3%83%93%E3%82%A4%E3%82%B9%E3%81%A8'
#       Benchmark.bm do |bm|
#         # Pre-Patch
#         bm.report("   Rack.unescape")    {1_000_000.times { Rack::Utils.unescape s } }
#         bm.report("    Rack.rfc2822")    {1_000_000.times { Rack::Utils.rfc2822(Time.now) } }
#         bm.report("    Rack.rfc2109")    {1_000_000.times { Rack::Utils.rfc2109(Time.now) } }
#         # Perform Patch
#         Iodine.patch_rack
#         puts "            --- Monkey Patching Rack ---"
#         # Post Patch
#         bm.report("Patched.unescape")    {1_000_000.times { Rack::Utils.unescape s } }
#         bm.report(" Patched.rfc2822")    {1_000_000.times { Rack::Utils.rfc2822(Time.now) } }
#         bm.report(" Patched.rfc2109")    {1_000_000.times { Rack::Utils.rfc2109(Time.now) } }
#       end && nil
#
# Results:
#         user     system      total        real
#         Rack.unescape  8.660000   0.010000   8.670000 (  8.687807)
#         Rack.rfc2822  3.730000   0.000000   3.730000 (  3.727732)
#         Rack.rfc2109  3.020000   0.010000   3.030000 (  3.031940)
#                      --- Monkey Patching Rack ---
#         Patched.unescape  0.340000   0.000000   0.340000 (  0.341506)
#         Patched.rfc2822  0.740000   0.000000   0.740000 (  0.737796)
#         Patched.rfc2109  0.690000   0.010000   0.700000 (  0.700155)
#
# At the moment, the extent of the monkey patching offered is very limited.
# As new optimizations are added, the policy regarding monkey patching (benifit vs. risks) might be re-evaluated.
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
  # However, `warmup` might cause undefined behavior and should be avoided when using gems that initiate network / database connections or gems that spawn threads (i.e., ActiveRecord / ActiveCable).
  #
  # Use {warmup} when either {processes} or {threads} are set to more then 1 and gems don't spawn threads or initialize network connections.
  def self.warmup app
    # load anything marked with `autoload`, since autoload isn't thread safe nor fork friendly.
    Iodine.run do
      Module.constants.each do |n|
        begin
          Object.const_get(n)
        rescue Exception => _e
        end
      end
      ::Rack::Builder.new(app) do |r|
        r.warmup do |a|
          client = ::Rack::MockRequest.new(a)
          client.get('/')
        end
      end
    end
  end

  def self.patch_rack
    ::Rack::Utils.class_eval do
      Iodine::Base::MonkeyPatch::RackUtils.methods(false).each do |m|
        ::Rack::Utils.define_singleton_method(m,
              Iodine::Base::MonkeyPatch::RackUtils.instance_method(m) )
      end
    end
  end

  def self.patch_json
    ::JSON.class_eval do
        ::JSON.define_singleton_method(:parse,
              Iodine::JSON.instance_method(:parse) )
    end
  end


  @after_fork_blocks = []
  def self.after_fork(*args, &block)
    if(block)
      @after_fork_blocks << [args, block]
    else
      @after_fork_blocks.each {|b| b[1].call(b[0]) }
    end
  end

  @before_fork_blocks = []
  def self.before_fork(*args, &block)
    if(block)
      @before_fork_blocks << [args, block]
    else
      @before_fork_blocks.each {|b| b[1].call(b[0]) }
    end
  end

  self.default_pubsub = ::Iodine::PubSub::CLUSTER
end

if(!defined?(after_fork))
  def after_fork(*args, &block)
    Iodine.after_fork(*args, &block)
  end
end
if(!defined?(on_worker_boot))
  def on_worker_boot(*args, &block)
    Iodine.after_fork(*args, &block)
  end
end
if(!defined?(before_fork))
  def before_fork(*args, &block)
    Iodine.before_fork(*args, &block)
  end
end


require 'rack/handler/iodine' unless defined? ::Iodine::Rack::IODINE_RACK_LOADED
