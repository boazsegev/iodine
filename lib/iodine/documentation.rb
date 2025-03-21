if !defined?(Iodine)
# Iodine is a high performance HTTP / WebSocket server and Evented Network Tool Library.
# 
# In essence, Iodine is a partial Ruby port for the [facil.io](http://facil.io) C library.
#
# Here is a simple text echo server using Iodine (see full list at {Iodine::Connection}):
#
#
#        require 'iodine'
#        
#        # Define the protocol for our service
#        module EchoProtocol
#        
#          # Called when a new connection is opened.
#          def on_open(client)
#            # Write a welcome message
#            puts "MY LOG: Client #{client.object_id} opened";
#            client.write "Echo server running on Iodine #{Iodine::VERSION}.\r\n"
#          end
#          
#          # this is called for incoming data - note data might be fragmented.
#          def on_message(client, data)
#            # write the data we received
#            client.write "echo: #{data}"
#            # close the connection when the time comes
#            client.close if data =~ /^bye[\n\r]/i
#          end
#          
#          # called if the connection is still open and the server is shutting down.
#          def on_shutdown(client)
#            # write the data we received
#            client.write "Server going away\r\n"
#          end
#          
#          # Called once all calls to `write` have been sent on the wire.
#          def on_drained(client)
#            puts "MY LOG: Client #{client.object_id} outgoing buffer now empty";
#          end
#          
#          # Called when `ping` time has elapsed with no data transferred on the wire.
#          def on_timeout(client)
#            puts "MY LOG: Client #{client.object_id} timing out";
#            # Write something to reset timeout.
#            client.write "Server message: are you there?\r\n"
#          end
#          
#          # Called when the connection is closed.
#          def on_close(client)
#            # Log closure
#            puts "MY LOG: Client #{client.object_id} closed";
#          end
#          
#          # We can use a singleton pattern,
#          # as the Protocol object doesn't contain any per-connection data.
#          extend self
#        end
#        
#        # create the service instance, the block returns a connection handler.
#        Iodine.listen(handler: EchoProtocol, service: :tcp)
#        # start the service
#        Iodine.threads = 1
#        Iodine.start
#
#
#
# Methods for setting up and starting {Iodine} include {start}, {threads}, {threads=}, {workers} and {workers=}.
#
# Methods for setting startup / operational callbacks include {on_state}.
#
# Methods for asynchronous execution include {run} (same as {defer}), {run_after} and {run_every}.
#
# Methods for application wide pub/sub include {subscribe}, {unsubscribe} and {publish}. Connection specific pub/sub methods are documented in the {Iodine::Connection} class).
#
# Methods for TCP/IP, Unix Sockets and HTTP connections include {listen} and {connect}.
#
# Note that the HTTP server supports both TCP/IP and Unix Sockets as well as SSE / WebSockets extensions.
#
# Iodine doesn't call {patch_rack} automatically, but doing so will improve Rack's performace.
#
# Please read the {file:README.md} file for an introduction to Iodine.
#
module Iodine

  # [Number] The number of worker threads per worker process. Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  def self.threads(); end
  # [Number] The number of worker threads per worker process. Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  def self.threads=(threads); end
  # [Number] The worker processes. Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  def self.workers(); end
  # [Number] The worker processes. Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  def self.workers=(workers); end

  # Starts the Iodine IO reactor. Method returns only after Iodine is stopped and cleanup is complete.
  def self.start(); end
  # Stops the Iodine IO reactor (as if `SIGINT` or `SIGTERM` were detected. Causes {start} to return.
  def self.stop(); end

  # [Boolean] Returns `true` if current process is the master process.
  def self.master?(); end
  # [Boolean] Returns `true` if current process is a worker process.
  # 
  # Note: may also be the master process if {Iodine.workers} was zero (non-cluster mode).
  def self.worker?(); end
  # [Boolean] Returns `true` if the Iodine IO reactor is running in the current process and the IO reactor wasn't signaled to stop.
  def self.running?(); end

  # [Number] The level of verbosity used for Iodine logging.
  def self.verbosity(); end
  # [Number] The level of verbosity used for Iodine logging.
  # 
  # Each level of logging includes all lower levels.
  # 
  # - 5 == Debug messages.
  # - 4 == Normal.
  # - 3 == Warnings.
  # - 2 == Errors.
  # - 1 == Fatal Errors only.
  def self.verbosity=(logging_level); end

  # [Number] The number of milliseconds before the reactor stops scheduling unperformed tasks during shutdown.
  def self.shutdown_timeout(); end
  # [Number] Sets the number of milliseconds before the reactor stops scheduling unperformed tasks during shutdown.
  def self.shutdown_timeout=(milliseconds); end

  # Listens on the `url` to connections requesting `service`.
  # 
  # The `handler` handles the following events and supports the following callbacks:
  # 
  # | | |
  # |---|---|
  # | `on_http(client)` | Called when an HTTP request arrives. |
  # | `on_authenticate_websocket(client)` | Called when a WebSocket Upgrade request is detected, **must** return `true` or the request is denied. |
  # | `on_open(client)` | Called when a WebSocket or `raw`/`tcp`connection had been established. |
  # | `on_message(client, data)` | Called when a WebSocket message arrives. For `raw`/`tcp` connections, `data` contains the part of the byte stream that was read by Iodine. |
  # | `on_drained(client)` | Called when all the calls to `client.write` have been completed (the outgoing buffer has been sent). |
  # | `on_timeout(client)` | Called when `ping` time has elapsed with no data was exchanged on the wire. |
  # | `on_shutdown(client)` | Called if the connection is still open and the server is shutting down. |
  # | `on_close(client)` | Called when the connection is closed |
  # | `on_authenticate_sse(client)` | Called when an Event Source (SSE) request is detected, **must** return `true` or the request is denied. |
  # | `on_eventsource_reconnect(client)` | Called when an Event Source (SSE) request contains re-connection data. |
  # | `on_eventsource(client, event)` | Called when an Event Source (SSE) event is received (client mode). |
  # | `on_authenticate(client)` | **MAY** be defined instead of `on_authenticate_websocket` and `on_authenticate_sse` to unify the logic of both authentication callbacks. |
  #
  # Accepts the following named arguments:
  # 
  # | | |
  # |---|---|
  # | url | The address to listen at (or `nil`). |
  # | handler | The handler (callback) object / NeoRack application / Rack application  |
  # | service | The service to be listening to (`http`, `https`, `ws`, `tcp`, `unix`). Can be provided as part of the `url`'s `scheme`, i.e.: `"unix://./my_unix_socket.sock"`. |
  # | tls | An Iodine::TLS instance. |
  # | public | (HTTP only) Public folder for static file service. |
  # | max_age | (HTTP only) The default maximum age for caching (when the etag header is provided). |
  # | max_header_size | (HTTP only) The maximum size for HTTP headers. |
  # | max_line_len | (HTTP only) The maximum size for a single HTTP header. |
  # | max_body_size | (HTTP only) The maximum size for an HTTP payload. |
  # | max_msg_size | (WebSocket only) The maximum size for a WebSocket message. |
  # | timeout | (HTTP only) `keep-alive` timeout. |
  # | ping | Connection timeout (WebSocket / `raw` / `tcp`). |
  # | log | (HTTP only) If `true`, logs `http` requests. |
  # | &block | An (optional) Rack application. |
  #
  # Note: Either a `handler` or a `block` (Rack App) **must** be provided.
  def self.listen(url = "0.0.0.0:3000", handler = nil, service = nil); end

  # Sets a block of code to run when Iodine's core state is updated.
  # 
  # @param state [Symbol] event the state event for which the block should run (see list).
  # 
  # The state event Symbol can be any of the following:
  # 
  # |  |  |
  # |---|---|
  # | `:pre_start`      | the block will be called once before starting up the IO reactor. |
  # | `:before_fork`    | the block will be called before each time the IO reactor forks a new worker. |
  # | `:after_fork`     | the block will be called after each fork (both in parent and workers). |
  # | `:enter_child`    | the block will be called by a worker process right after forking. |
  # | `:enter_master`   | the block will be called by the master process after spawning a worker (after forking). |
  # | `:start`          | the block will be called every time a *worker* process starts. In single process mode, the master process is also a worker. |
  # | `:idle`           | the block will be called every time the IO reactor enters an idling state (on all processes). |
  # | `:parent_crush`   | the block will be called by each worker the moment it detects the master process crashed. |
  # | `:child_crush`    | the block will be called by the parent (master) after a worker process crashed. |
  # | `:start_shutdown` | the block will be called before starting the shutdown sequence. |
  # | `:stop`           | the block will be called just before stopping iodine (both on child and parent processes). |
  # | `:exit`           | the block will be called during iodine library cleanup, similar to a Ruby `at_exit` callback. |
  # 
  # Code runs in both the parent and the child.
  def self.on_state(state, &block); end

  # Runs a block of code in the main IO thread (adds the code to the event queue).
  # 
  # Always returns the block of code to executed (Proc object).
  # 
  # Code will be executed only while Iodine is running (after {Iodine.start}).
  # 
  # Code blocks that where scheduled to run before Iodine enters cluster mode will run on all child processes.
  # 
  # The code will always run in single-threaded mode, running on the main IO thread used by Iodine.
  # 
  # Note: blocking code MUST NOT be used in the block passed to this method,
  # or Iodine's IO will hang while waiting for the blocking code to finish.
  # 
  # Note: method also accepts a method object if passed as a parameter.
  def self.run(&block); end 

  # Runs a block of code in the a worker thread (adds the code to the event queue).
  # 
  # Always returns the block of code to executed (Proc object).
  # 
  # Code will be executed only while Iodine is running (after {Iodine.start}).
  # 
  # Code blocks that where scheduled to run before Iodine enters cluster mode will run on all child processes.
  # 
  # The code may run in parallel with other calls to `run` or `async`, as it will run in one of the worker threads.
  # 
  # Note: method also accepts a method object if passed as a parameter.
  def self.async(&block); end 

  # Runs the required block after the specified number of milliseconds have passed.
  # 
  # The block will run in the main IO thread (adds the code to the event queue).
  # 
  # Time is counted only once Iodine started running (using {Iodine.start}).
  # 
  # Accepts:
  # 
  # |   |   |
  # |---|---|
  # | `:milliseconds` | the number of milliseconds between event repetitions. |
  # | `:repetitions` | the number of event repetitions. Defaults to 1 (performed once). Set to 0 for never ending. |
  # | `:block` | (required) a block is required, as otherwise there is nothing to perform. |
  # 
  # 
  # The event will repeat itself until the number of repetitions had been depleted or until the event returns `false`.
  # 
  # Always returns a copy of the block object.
  def self.run_after(milliseconds, repetitions = 1, &block); end 

  # Publishes a message to a combination of a channel (String) and filter (number).
  # 
  # Accepts the following (possibly named) arguments:
  # 
  # - `channel`: the channel name to publish to (String).
  # - `filter`: the filter to publish to (Number < 32,767).
  # - `message`: the actual message to publish.
  # - `engine`: the pub/sub engine to use (if not the default one).
  # 
  # i.e.:
  # 
  #     Iodine.publish("channel_name", 0, "payload")
  #     Iodine.publish(channel: "name", message: "payload")
  def self.publish(channel = nil, filter = 0, message = nil, engine = nil); end
  # Subscribes to a combination of a channel (String) and filter (number).
  # 
  # Accepts the following (possibly named) arguments and an **optional** block:
  # 
  # - `channel`: the subscription's channel name (String).
  # - `filter`: the subscription's filter (Number < 32,767).
  # - `callback`: an **optional** object that answers to call and accepts a single argument (the message structure).
  # 
  # Either a proc or a block **must** be provided for global subscriptions.
  # 
  # i.e.:
  # 
  #     Iodine.subscribe("name")
  #     Iodine.subscribe(channel: "name")
  #     # or with filter
  #     Iodine.subscribe("name", 1)
  #     Iodine.subscribe(channel: "name", filter: 1)
  #     # or only a filter
  #     Iodine.subscribe(nil, 1)
  #     Iodine.subscribe(filter: 1)
  #     # with / without a proc
  #     Iodine.subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil;}
  #     Iodine.subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
  def self.subscribe(channel = nil, filter = 0, &callback); end
  # Un-Subscribes to a combination of a channel (String) and filter (number).
  # 
  # Accepts the following (possibly named) arguments:
  # 
  # - `channel`: the subscription's channel name (String).
  # - `filter`: the subscription's filter (Number < 32,767).
  # 
  # i.e.:
  # 
  #     Iodine.unsubscribe("name")
  #     Iodine.unsubscribe(channel: "name")
  #     # or with filter
  #     Iodine.unsubscribe("name", 1)
  #     Iodine.unsubscribe(channel: "name", filter: 1)
  #     # or only a filter
  #     Iodine.unsubscribe(nil, 1)
  #     Iodine.unsubscribe(filter: 1)
  def self.unsubscribe(channel = nil, filter = 0); end

  #######################

  # {Iodine::JSON} exposes the {Iodine::Connection#write} fallback behavior when called with non-String objects.
  # 
  # The fallback behavior is similar to (though faster than) calling:
  # 
  #     client.write(Iodine::JSON.stringify(data))
  # 
  # If you want to work with JSON, consider using the `oj` gem.
  # 
  # This API is mostly to test for Iodine JSON input/output errors and reflects what the C layer sees.
  # 
  # ## Performance
  # 
  # Performance... could be better.
  # 
  # Converting Ruby objects into a JSON String (Strinfigying) should be fast even though the String data is copied twice, once to C and then to Ruby.
  # 
  # However, Converting a JSON object into Ruby Objects is currently slow and it is better to use the `oj` gem or even the Ruby builtin parser.
  # 
  # The reason is simple - the implementation is designed to create C objects (C Hash Maps, C Arrays, etc'), not Ruby objects. When converting from a String to Ruby Objects, the data is copied twice, once to C and then to Ruby.
  # 
  # This especially effects parsing, where more objects are allocated, whereas {Iodine::JSON.stringify} only (re)copies the String data which is a single continuous block of memory.
  # 
  # That's why {Iodine::JSON.stringify} is significantly faster than the Ruby `object.to_json` approach, yet slower than `JSON.parse(json_string)`.
  # 
  # Performance depends on architecture and compiler used. Please measure:
  # 
  #     require 'iodine/benchmark'
  #     Iodine::Benchmark.json
  module JSON
    # Accepts a JSON String and returns a Ruby object.
    def self.parse(json_string); end
    # Accepts a Ruby object and returns a JSON String.
    def self.stringify(ruby_object); end
  end

  #######################

  # {Iodine::Mustache} is a lighter implementation of the mustache template rendering gem, with a focus on a few minor security details:
  # 
  # 1. HTML escaping is more aggressive, increasing XSS protection. Read why at: [wonko.com/post/html-escaping](https://wonko.com/post/html-escaping).
  # 
  # 2. Dot notation is tested in whole as well as in part (i.e. `user.name.first` will be tested as is, than the couplet `user`, `name.first` and than as each `user`, `name` , `first`), allowing for the Hash data to contain keys with dots while still supporting dot notation shortcuts.
  # 
  # 3. Less logic: i.e., lambdas / procs do not automatically invoke a re-rendering... I'd remove them completely as unsafe, but for now there's that.
  # 
  # 4. Improved Protection against Endless Recursion: i.e., Partial templates reference themselves when recursively nested (instead of being recursively re-loaded); and Partial's context is limited to their starting point's context (cannot access parent context).
  # 
  # It wasn't designed specifically for speed or performance... but it ended up being significantly faster.
  # 
  # ## Usage
  # 
  # This approach to Mustache templates may require more forethought when designing either the template or the context's data format, however it should force implementations to be more secure and performance aware.
  # 
  # Approach:
  # 
  #     require 'iodine'
  #     # One-off rendering of (possibly dynamic) template:
  #     result = Iodine::Mustache.render(template: "{{foo}}", ctx: {foo: "bar"}) # => "bar"
  #     # caching of parsed template data for multiple render operations:
  #     view = Iodine::Mustache.new(file: "./views/foo.mustache", template: "{{foo}}")
  #     results = Array.new(100) {|i| view.render(foo: "bar#{i}") } # => ["bar0", "bar1", ...]
  # 
  # ## Performance
  # 
  # Performance may differ according to architecture and compiler used. Please measure:
  # 
  #     require 'iodine/benchmark'
  #     Iodine::Benchmark.mustache
  #
  # **Note**: the benchmark requires the `mustache` gem, which could be installed using:
  # 
  #     gem install mustache
  #
  # See also {file:./mustache.md}.
  class Mustache
    # Loads a template file and compiles it into a flattened instruction tree.
    # 
    #        Iodine::Mustache.new(file = nil, data = nil, on_yaml = nil, &block)
    # 
    # @param file [String] a file name for the mustache template.
    # 
    # @param template [String] the content of the mustache template - if `nil` or missing, `file` content will be loaded from disk.
    # 
    # @param on_yaml [Proc] (**optional**) accepts a YAML front-matter String.
    # 
    # @param &block [Block] to be used as an implicit `on_yaml` (if missing).
    # 
    # @return [Iodine::Mustache] returns an Iodine::Mustache object with the provided template ready for rendering.
    # 
    # **Note**: Either the file or template argument (or both) must be provided.
    def initialize(file = nil, template = nil, on_yaml = nil, &block); end
   
    # Renders the template given at initialization with the provided context.
    #
    #        m.render(ctx)
    #
    # @param ctx [Hash] the top level context for the template data.
    #
    # @return [String] returns a String containing the rendered template.
    def render(ctx = nil); end

    # Loads a template file and renders it into a String.
    #
    #        Iodine::Mustache.render(file = nil, data = nil, ctx = nil, on_yaml = nil)
    #
    # @param file [String] a file name for the mustache template.
    #
    # @param template [String] the content of the mustache template - if `nil` or missing, `file` content will be loaded from disk.
    #
    # @param ctx [Hash] the top level context for the template data.
    #
    # @param on_yaml [Proc] (**optional**) accepts a YAML front-matter String.
    #
    # @param &block [Block] to be used as an implicit \p on_yaml (if missing).
    #
    # @return [String] returns a String containing the rendered template.
    #
    # **Note**: Either the file or template argument (or both) must be provided.
    def self.render(file = nil, template = nil, ctx = nil, on_yaml = nil, &block); end
  end

  #######################

  # These are some utility, unescaping and decoding helpers provided by Iodine.
  # 
  # These **should** be faster then their common Ruby / Rack alternative, but this may depend on your own machine and Ruby version.
  # 
  #     require 'iodine/benchmark'
  #     Iodine::Benchmark.utils
  # 
  # After testing the performance, consider calling {Iodine::Utils.monkey_patch} if using Rack.
  module Utils

    # Encodes a String using percent encoding (i.e., URI encoding).
    def self.escape_path()  ;  end
    # Encodes a String in place using percent encoding (i.e., URI encoding).
    def self.escape_path!()  ;  end
    # Decodes percent encoding, including the `%uxxxx` JavaScript extension.
    def self.unescape_path()  ;  end
    # Decodes percent encoding in place, including the `%uxxxx` JavaScript.
    def self.unescape_path!()  ;  end
    # Encodes a String using percent encoding (i.e., URI encoding).
    def self.escape()  ;  end
    # Encodes a String using percent encoding (i.e., URI encoding).
    def self.escape!()  ;  end
    # Decodes percent encoding, including the `%uxxxx` JavaScript extension and converting `+` to spaces.
    def self.unescape()  ;  end
    # Decodes percent encoding in place, including the `%uxxxx` JavaScript extension and converting `+` to spaces.
    def self.unescape!()  ;  end
    # Escapes String using HTML escape encoding.
    #
    # Note: this function may significantly increase the number of escaped characters when compared to the native implementation.
    def self.escape_html()  ;  end
    # Escapes String in place using HTML escape encoding.
    # 
    # Note: this function may significantly increase the number of escaped characters when compared to the native implementation.
    def self.escape_html!()  ;  end
    # Decodes an HTML escaped String.
    def self.unescape_html()  ;  end
    # Decodes an HTML escaped String in place.
    def self.unescape_html!()  ;  end
    # Takes a Time object and returns a String conforming to RFC 2109.
    def self.rfc2109(time) ;  end
    # Takes a Time object and returns a String conforming to RFC 2822.
    def self.rfc2822(time) ;  end
    # Takes a Time object and returns a String conforming to RFC 7231.
    def self.time2str(time) ;  end
    # Securely compares two String objects to see if they are equal.
    # 
    # Designed to be secure against timing attacks when both String objects are of the same length.
    # 
    # Performance depends on specific architecture and compiler used. Always measure:
    # 
    #     require 'iodine'
    #     require 'rack'
    #     require 'benchmark'
    #     def prove_secure_compare(name, mthd, length = 4096)
    #       GC.disable
    #       a = 0;
    #       b = 0;
    #       str1 = Array.new(length) { 'a' } .join; str2 = Array.new(length) { 'a' } .join;
    #       counter = 1.0 / (Benchmark.measure {1024.times {mthd.call(str1, str2)}}).total
    #       counter = counter / 2 
    #       counter = 1.0 if(counter < 1.0)
    #       counter = counter.to_i
    #       bm = Benchmark.measure do
    #         2048.times do
    #           tmp = Benchmark.measure {counter.times {mthd.call(str1, str2)}}
    #           str1[0] = 'b'
    #           tmp2 = Benchmark.measure {counter.times {mthd.call(str1, str2)}}
    #           str1[0] = 'a'
    #           tmp = tmp2.total - tmp.total
    #           a += 1 if tmp >= 0
    #           b += 1 if tmp <= 0
    #         end
    #       end
    #       GC.enable
    #       counter = (a.to_f/b.to_f - b.to_f/a.to_f).abs
    #       msg = ""
    #       msg = "DIFFERENCE TOO LARGE!" if (a == 0 || b == 0 || counter > 0.3)
    #       puts "#{name} timing ratio #{a}:#{b} #{msg} (#{'%.3f' % counter})\n#{bm.to_s}\n"
    #     end
    #     def print_proof
    #         methods = [ ["String == (short string)", (Proc.new {|a,b| a == b } )],
    #                     ["Iodine::Utils.secure_compare (short string)", Iodine::Utils.method(:secure_compare)],
    #                     ["Rack::Utils.secure_compare (short string)", Rack::Utils.method(:secure_compare)] ]
    #         [64, 2048].each {|len| methods.each {|c| prove_secure_compare(c[0], c[1], len) } }
    #     end
    #     print_proof
    # 
    def self.secure_compare() ;  end
    # Adds the `Iodine::Utils` methods to the modules passed as arguments.
    # 
    # If no modules were passed to the `monkey_patch` method, `Rack::Utils` will be
    # monkey patched.
    def self.monkey_patch() ;  end

  end


  #######################

  # This is the namespace for all pub/sub related classes and constants.
  module PubSub
    # Iodine::PubSub::Message class instances are passed to subscription callbacks.
    #
    # This allows subscription callbacks to get information about the pub/sub event.
    class Message
      # Returns the event's `id` property - a (mostly) random numerical value.
      def id(); end
      # Returns the event's `channel` property - the event's target channel.
      def channel(); end
      alias :event :channel
      # Returns the event's `filter` property - the event's numerical channel filter.
      def filter(); end
      # Returns the event's `message` property - the event's payload (same as {to_s}).
      def message(); end
      alias :msg :message
      alias :data :message
      # Returns the event's `published` property - the event's published timestamp in milliseconds since epoch.
      def published(); end

      # Returns the event's `message` property - the event's payload (same as {message}).
      def to_s(); end

      # Sets the event's `id` property.
      def id=(); end
      # Sets the event's `channel` property.
      def channel=(); end
      # Sets the event's `filter` property.
      def filter=(); end
      # Sets the event's `message` property.
      def message=(); end
      # Sets the event's `published` property.
      def published=(); end
    end

    # Iodine::PubSub::Engine class instances are used as the base class for all pub/sub engines.
    #
    # The Engine class makes it easy to use leverage Iodine's pub/sub system using external services.
    # 
    # Engine child classes SHOULD override the #subscribe, #unsubscribe and #publish in order to perform this actions using the backend service (i.e. using Redis).
    # 
    # Once an Engine instance receives a message from its backend service, it should forward the message to the Iodine distribution layer one of the other engines. i.e.:
    # 
    #     Iodine.publish channel: "subscribed_channel", message: "the_message", engine: Iodine::PubSub::Engine::CLUSTER
    # 
    # Iodine comes with two built-in engines:
    # 
    # * {Iodine::PubSub::Engine::CLUSTER} will distribute messages to all subscribers in the process cluster.
    # 
    # * {Iodine::PubSub::Engine::PROCESS} will distribute messages to all subscribers sharing the same process.
    class Engine
      # When implementing your own initialization class, remember to call `super` to attach the engine to Iodine.
      def initialize; end
      # Please OVERRIDE(!) – called by iodine when a message was published to the engine (using `Iodine.publish(engine: MY_ENGINE, ...)).
      def pubslish(msg); end
      # Please OVERRIDE(!) – called by iodine when a named channel was subscribed to.
      def subscribe(channel); end
      # Please OVERRIDE(!) – called by iodine when a pattern channel was subscribed to.
      def psubscribe(pattern); end
      # Please OVERRIDE(!) – called by iodine when a named channel was unsubscribed to.
      def unsubscribe(channel); end
      # Please OVERRIDE(!) – called by iodine when a pattern channel was unsubscribed to.
      def punsubscribe(pattern); end
      # Please OVERRIDE(!) – called by iodine when a the engine is detached from iodine.
      # 
      # This happens when the engine object went out of scope and should be collected by the GC (Garbage Collector).
      def on_cleanup; end

      # This engine publishes the message to all subscribers in Iodine's root process.
      ROOT = Engine.new
      # This engine publishes the message to all subscribers in the specific root or worker process where `Iodine.publish` was called.
      PROCESS = Engine.new
      # This engine publishes the message to all subscribers in Iodine's worker processes (except current process).
      SIBLINGS = Engine.new
      # This engine publishes the message to all subscribers in Iodine's root and worker processes.
      LOCAL = Engine.new
      # This engine publishes the message to all subscribers in Iodine's root and worker processes, as well as any detected Iodine instance on the local network.
      CLUSTER = Engine.new
    end

  end

  #######################

  # {Iodine::Connection} is the connection instance class used for all connection callbacks to control the state of the connection.
  # 
  # The {Iodine::Connection} class offers methods that control the different aspects of a Connection's state:
  #
  # - HTTP related methods include the {#status}, {#method}, {#path}, {#query}, {#headers}, {#cookie}, {#each_cookie}, {#read}, {#env} (Rack), and more methods.
  # 
  # - WebSocket / TCP related methods include the {#open?}, {#write}, {#pending} and {#close} methods.
  # 
  # - Pub/Sub related methods include the {#subscribe}, {#unsubscribe}, {#publish} and {#pubsub?} methods.
  # 
  # - Connection specific data can be stored / accessed using {#[]}, {#[]=}, {#each}, {#store_count}, {#store_capa}, {#store_reserve} methods.
  # 
  class Connection

    # Unless `env` is manually set, Iodine will attempt to create a default `env` object that will follow the Rack Server specification for HTTP connections.
    #
    # Otherwise, the `env` attribute is usually a Hash object set by the client and may be used as an alternative to the Connection data storage (see {#[]} and {#[]=}).
    attr_accessor :env
    # The `handler` attribute (see {Iodine::Connection::Handler}) - this may be the Rack application or a [NeoRack](https://github.com/boazsegev/neorack) application.
    attr_accessor :handler
    # For HTTP connections, the `status` value is the HTTP status value that will be sent in the response.
    attr_accessor :status
    # For HTTP connections, the `method` value is the HTTP method value received by the client.
    attr_accessor :method
    # For HTTP connections, the `path` value is the HTTP URL's path String received by the client.
    attr_accessor :path
    # For HTTP connections, the `query` value is the HTTP URL's query String (if received by the client).
    attr_accessor :query
    # For HTTP connections, the `version` String value stated by the HTTP client.
    attr_accessor :version
    # For HTTP connections, sets all incoming request headers in the Connection instance storage ({#[]}) and returns `self`.
    #
    # Used to overcome lazy parsing of incoming headers. i.e.:
    # 
    #     # prints all incoming headers, assuming NeoRack guidelines were followed:
    #     client.headers.each do |key, value|
    #         if(value.is_a? Array)
    #             value.each {|v| puts "#{key}: #{v}" }
    #         else
    #             puts "#{key}: #{value}"
    #         end
    #     end
    # 
    attr_accessor :headers
    # (HTTP Only) Returns the length of the `body` payload.
    attr_reader :length

    # Returns a the value of a key previously set.
    #
    # If the Connection is an HTTP connection and `key` is a Header String, the value of the header (if received) will be returned.
    # 
    # If no value was set for the key (or no Header received), `nil` will be returned.
    def [](key); end
    # Sets a key value pair.
    def []=(key, value); end
    # Iterates over the stored key value pairs.
    #
    # If the Connection is an HTTP connection, `each` will also iterate over any header previously accessed.
    def each(&block); end
    # Returns the number of key value pairs stored.
    def store_count; end
    # Returns the theoretical capacity of the key value pair storage map.
    def store_capa; end
    # Reserved a theoretical storage capacity in the key value pair storage map.
    #
    # Note that the capacity is theoretical only and some extra memory is required to allow for possible (partial) hash collisions.
    def store_reserve(number_of_items); end

    # Returns `true` if the connection appears to be open (no known issues).
    #
    # (HTTP Only) Returns `true` only if upgraded and open (WebSocket is open).
    def open?(); end
    # @deprecated Please check the {Iodine.extensions} Hash instead.
    # 
    # Always returns `true`, since Iodine connections support the pub/sub extension.
    # 
    # This method should be considered deprecated and will be removed in future versions.
    def pubsub?(); end
    # Returns the number of bytes that need to be sent before the next `on_drained` callback is called.
    def pending(); end
    # Schedules the connection to be closed.
    def close(); end
    # Sets a response header and returns `true`.
    # 
    # If headers have already been sent, or if either `write` or `finish` has been previously called, returns `false` (doing nothing).
    # 
    # Header `name` should be a lowercase String. This will be enforced by Iodine, converting header names to lower case and converting Symbols to Strings.
    #
    # `value` must be either a `String`, an `Array` of Strings, or `nil`. Iodine also accepts Symbols and certain Numbers (small numbers).
    # 
    # **If `value` is `nil`**:
    #  - Performs a non-guaranteed best attempt at removing any existing headers named `name` and returns `false`.
    #  
    # **If `value` is a `String`**:
    #  - A response header with the given `name` is added to the response and set to `value`. If `name` already exists, multiple headers will be sent.
    #  - Iodine is backwards compatible with Rack 2, treating newline characters as an array of `String`s. Eventually this will be removed.
    #
    # **If `value` is an `Array` of `String`s**:
    #  - The method behaves as if called multiple times with the same `name`, once for each element of `value` (except `nil` values are ignored).
    def write_headers(name, values); end
    # Writes data to the connection asynchronously.
    #
    # If `data` **should* be a String. However, if `data` is not a String, Iodine will attempt to convert it to a JSON String, allowing Hashes, Arrays and other native Ruby objects to be sent over the wire. Note that `data` **must** be a native Ruby Object, or it could not be converted to a JSON String.
    # 
    # For WebSocket connections: if `data` is a UTF-8 encoded String, message will be sent as `text`, otherwise message will be sent as `binary`.
    # 
    # For SSE connections: this is similar to calling `write_sse(nil, nil, data)`.
    # 
    # Returns `false` on error / fail and `true` if the event was scheduled to be sent to the client.
    def write(data); end
    # Writes data to EventSource client connections asynchronously.
    #
    # The `id` and `event` elements **must** be UTF-8 encoded Strings.
    # 
    # If `data` **should* be a String. However, if `data` is not a String, Iodine will attempt to convert it to a JSON String, allowing Hashes, Arrays and other native Ruby objects to be sent over the wire. Note that `data` **must** be a native Ruby Object, or it could not be converted to a JSON String.
    # 
    # An empty `data` String or a missing `data` argument will cause `write_sse` to return false.
    # 
    # Returns `false` on error / fail and `true` if the event was scheduled to be sent to the client.
    def write_sse(id, event, data); end
    # Closes the connection after writing `data`, or (HTTP Only) finishes the HTTP response.
    #
    # Note that for HTTP connections the `"content-length"` header is automatically written if no previous calls to `write` were called.
    def finish(data = nil); end


    # (HTTP Only) Returns `false` if headers can still be sent. Otherwise returns `true`.
    def headers_sent?; end
    # Returns `true` only if the Connection object is still valid (data can be sent).
    def valid?; end
    # Returns true only of the connection is a WebSocket connection.
    def websocket?; end
    # Returns true only of the connection is an SSE connection.
    def sse?; end


    # (HTTP Only) Returns an ASCII-8 String with the next line in the Body (same as {::IO#gets} where only `limit` is allowed).
    def gets(limit = nil); end
    # (HTTP Only) Returns an ASCII-8 String with (part of) the Body (see {::IO#read}).
    def read(maxlen = nil, out_string = nil); end
    # (HTTP Only) Sets the body's new read position. Negative values start at the end of the body.
    def seek(pos = 0); end


    # (HTTP Only) Returns an ASCII-8 String with the value of the named cookie.
    def cookie(name); end
    # (HTTP Only) Sets the value of the named cookie and returns `true` upon success.
    #
    # If `headers_sent?` returns `true`, calling this method may raise an exceptions.
    # 
    # If `value` is `nil`, the cookie will be deleted. If `:max_age` is 0 (default), cookie will be session cookie and will be deleted by the browser at its discretion.
    # 
    # This should behave similar to calling `write_header`, except that the cookie can be read using the {#cookie} method.
    # 
    # Note that this method accepts named arguments. i.e.:
    # 
    #     set_cookie(name: "MyCookie", value: "My non-secret data", domain: "localhost", max_age: 1_728_000)
    # 
    # For more details, see: https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Set-Cookie
    def set_cookie(name, value = nil, max_age = 0, domain = nil, path = nil, same_site = nil, secure = false, http_only = false, partitioned = false); end
    # Calls `block` for each name-value cookie pair.
    def each_cookie(&block); end

    # Publishes a message to a combination of a channel (String) and filter (number).
    # 
    # Accepts the following (possibly named) arguments:
    # 
    # - `channel`: the channel name to publish to (String).
    # - `filter`: the filter to publish to (Number < 32,767).
    # - `message`: the actual message to publish.
    # - `engine`: the pub/sub engine to use (if not the default one).
    # 
    # i.e.:
    # 
    #     Iodine::Connection.publish("channel_name", 0, "payload")
    #     Iodine::Connection.publish(channel: "name", message: "payload")
    def self.publish(channel = nil, filter = 0, message = nil, engine = nil); end
    # Publishes a message to a combination of a channel (String) and filter (number).
    # 
    # **Note**: events published by a specific client are sent to "**everyone else**" and ignored by the client itself.
    # 
    # **Note**: if `engine` was specified and it isn't an Iodine engine, message **may** be delivered to the client calling `publish` as well.
    # 
    # To publish events to everyone, including the client itself, use the global {Iodine.publish} method or the {.publish} class method.
    # 
    # Accepts the following (possibly named) arguments:
    # 
    # - `channel`: the channel name to publish to (String).
    # - `filter`: the filter to publish to (Number < 32,767).
    # - `message`: the actual message to publish.
    # - `engine`: the pub/sub engine to use (if not the default one).
    # 
    # i.e.:
    # 
    #     client.publish("channel_name", 0, "payload")
    #     client.publish(channel: "name", message: "payload")
    # 
    def publish(channel = nil, filter = 0, message = nil, engine = nil); end

    # Subscribes to a combination of a channel (String) and filter (number).
    # 
    # Accepts the following (possibly named) arguments and an **optional** block:
    # 
    # - `channel`: the subscription's channel name (String).
    # - `filter`: the subscription's filter (Number < 32,767).
    # - `callback`: an **optional** object that answers to call and accepts a single argument (the message structure).
    # 
    # Either a proc or a block MUST be provided for global subscriptions.
    # 
    # i.e.:
    # 
    #     client.subscribe("name")
    #     client.subscribe(channel: "name")
    #     # or with filter
    #     client.subscribe("name", 1)
    #     client.subscribe(channel: "name", filter: 1)
    #     # or only a filter
    #     client.subscribe(nil, 1)
    #     client.subscribe(filter: 1)
    #     # with / without a proc
    #     client.subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil;}
    #     client.subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
    def subscribe(channel = nil, filter = 0, &callback); end
    # Subscribes to a combination of a channel (String) and filter (number).
    # 
    # Accepts the following (possibly named) arguments and an **optional** block:
    # 
    # - `channel`: the subscription's channel name (String).
    # - `filter`: the subscription's filter (Number < 32,767).
    # - `callback`: an **optional** object that answers to call and accepts a single argument (the message structure).
    # 
    # Either a proc or a block MUST be provided for global subscriptions.
    # 
    # i.e.:
    # 
    #     Iodine::Connection.subscribe("name")
    #     Iodine::Connection.subscribe(channel: "name")
    #     # or with filter
    #     Iodine::Connection.subscribe("name", 1)
    #     Iodine::Connection.subscribe(channel: "name", filter: 1)
    #     # or only a filter
    #     Iodine::Connection.subscribe(nil, 1)
    #     Iodine::Connection.subscribe(filter: 1)
    #     # with / without a proc
    #     Iodine::Connection.subscribe(filter: 1) {|msg| msg.filter == 1; msg.channel == Qnil;}
    #     Iodine::Connection.subscribe() {|msg| msg.filter == 1; msg.channel == Qnil; }
    def self.subscribe(channel = nil, filter = 0, &callback); end

    # Un-Subscribes to a combination of a channel (String) and filter (number).
    # 
    # Accepts the following (possibly named) arguments:
    # 
    # - `channel`: the subscription's channel name (String).
    # - `filter`: the subscription's filter (Number < 32,767).
    # 
    # i.e.:
    # 
    #     client.unsubscribe("name")
    #     client.unsubscribe(channel: "name")
    #     # or with filter
    #     client.unsubscribe("name", 1)
    #     client.unsubscribe(channel: "name", filter: 1)
    #     # or only a filter
    #     client.unsubscribe(nil, 1)
    #     client.unsubscribe(filter: 1)
    def unsubscribe(channel = nil, filter = 0); end
    # Un-Subscribes to a combination of a channel (String) and filter (number).
    # 
    # Accepts the following (possibly named) arguments:
    # 
    # - `channel`: the subscription's channel name (String).
    # - `filter`: the subscription's filter (Number < 32,767).
    # 
    # i.e.:
    # 
    #     Iodine::Connection.unsubscribe("name")
    #     Iodine::Connection.unsubscribe(channel: "name")
    #     # or with filter
    #     Iodine::Connection.unsubscribe("name", 1)
    #     Iodine::Connection.unsubscribe(channel: "name", filter: 1)
    #     # or only a filter
    #     Iodine::Connection.unsubscribe(nil, 1)
    #     Iodine::Connection.unsubscribe(filter: 1)
    def self.unsubscribe(channel = nil, filter = 0); end

    # Hijacks the connection from the Server and returns an IO object.
    #
    # This method MAY be used to implement a full hijack when providing compatibility with the Rack specification.
    #
    # @return [IO] for current underlying socket connection
    def rack_hijack; end

#######################
    
    # This is an example handler for documentation purposes.
    # 
    # This module is designed to help you author your own Handler for {Iodine.listen}, {Iodine::Connection.new} and {Iodine.connect}.
    # 
    # A handler implementation is expected to implement at least one of the callbacks defined for this example Handler. Any missing callbacks will be defined by Iodine using smart defaults (where possible).
    #
    # **Note**: This module isn't implemented by Iodine, but is for documentation and guidance only. 
    module Handler
      # Used for [NeoRack](https://github.com/boazsegev/neorack) application authoring, when receiving HTTP requests.
      #
      # [NeoRack](https://github.com/boazsegev/neorack) allows both Async and CGI style Web Applications, making it easier to stream data and do more with a single thread.
      def self.on_http(event); end
      # Used for [NeoRack](https://github.com/boazsegev/neorack) application authoring, when an HTTP request handling had finished.
      def self.on_finish(event); end
      # Used for [Rack](https://github.com/rack/rack) application authoring, when receiving HTTP requests.
      #
      # [Rack](https://github.com/rack/rack) is the classical server interface and it is supported by Iodine with minimal extensions.
      def self.call(env); end
      # When present, a missing `on_authenticate_sse` or missing `on_authenticate_websocket` will route to this callback, allowing authentication logic to be merged in one method.
      # 
      # **Must** return `true` for the connection to be allowed. **Any other return value will cause the connection to be refused** (and disconnected).
      #
      # By default, if `on_message` or `on_open` are defined, than `on_authenticate` returns `true`. Otherwise, `on_authenticate` returns `false`.
      def self.on_authenticate(connection); end
      # Called when an Event Source (SSE) request is detected, to authenticate and possibly set the identity of the user.
      #
      # By default, if `on_open` is defined, than `on_authenticate_sse` returns `true`. Otherwise, `on_authenticate` returns `false`.
      def self.on_authenticate_sse(connection); end
      # Called when a WebSocket Upgrade request is detected, to authenticate and possibly set the identity of the user.
      #
      # By default, if `on_message` or `on_open` are defined, than `on_authenticate_websocket` returns `true`. Otherwise, `on_authenticate` returns `false`.
      def self.on_authenticate_websocket(connection); end
      # Called when a long-running connection opens. This will be called for Event Source, WebSocket, TCP/IP and Raw Unix Socket connections.
      def self.on_open(connection); end
      # Called when incoming data arrives and its behavior depends on the underlying protocol.
      # 
      # For TCP/IP and Raw Unix connections, the data is a Binary encoded String with some (or all) of the data available in the incoming socket buffer.
      #
      # For WebSockets, the `data` element will contain a String with the WebSocket Message.
      # 
      # If the WebSocket message is in text, the String encoding will be UTF-8.
      # 
      # If the WebSocket message is binary, the String encoding will be Binary (ASCII).
      def self.on_message(connection, data); end
      # Called when all calls to {Iodine::Connection#write} have been handled and the outgoing buffer is now empty.
      def self.on_drained(connection); end
      # Called when timeout has been reached for a WebSocket / TCP/IP / Raw Unix Socket connection.
      def self.on_timeout(connection); end
      # Called when the worker that manages this connection (or the root process, in non-cluster mode) starts shutting down.
      def self.on_shutdown(connection); end
      # Called when the connection was closed (WebSocket / TCP/IP / Raw Unix Socket).
      def self.on_close(connection); end
      # Called when an Event Source (SSE) event has been received (when acting as an Event Source client).
      def self.on_eventsource(connection, message); end
      # Called an Event Source (SSE) client send a re-connection request with the ID of the last message received.
      def self.on_eventsource_reconnect(connection, last_message_id); end
    end

  end

  #######################

  # Used to setup a TLS contexts for connections (incoming / outgoing).
  class TLS
    # Assigns the TLS context a public certificate, allowing remote parties to validate the connection's identity.
    # 
    # A self signed certificate is automatically created if the `name` argument is specified and either (or both) of the `cert` (public certificate) or `key` (private key) arguments are missing.
    # 
    # Some implementations allow servers to have more than a single certificate, which will be selected using the SNI extension. I believe the existing OpenSSL implementation supports this option (untested).
    # 
    #      Iodine::TLS#add_cert(name = nil,
    #                           cert = nil,
    #                           key = nil,
    #                           password = nil)
    # 
    # Certificates and keys should be String objects leading to a PEM file.
    # 
    # This method also accepts named arguments. i.e.:
    # 
    #      tls = Iodine::TLS.new
    #      tls.add_cert name: "example.com"
    #      tls.add_cert cert: "my_cert.pem", key: "my_key.pem"
    #      tls.add_cert cert: "my_cert.pem", key: "my_key.pem", password: ENV['TLS_PASS']
    # 
    # Since TLS setup is crucial for security, an initialization error will result in Iodine crashing with an error message. This is expected behavior.
    def add_cert(name = nil, cert = nil, key = nil, password = nil); end
  end


  #######################

  # Used internally by Iodine.
  class Base

    # Prints the number of object withheld from the GC (for debugging).
    def self.print_debug(); end
    # Sets Iodine's cache limit for frozen strings, limited to 65,535 items.
    def self.cache_limit=(new_limit); end
    # Returns Iodine's cache limit for frozen strings.
    def self.cache_limit(); end


    # Parses and manages CLI input.
    module CLI
      # Parses CLI input.
      # 
      # If `required` is true and CLI detects an error, program will exit with an appropriate exit message and a list of valid CLI options.
      def parse(required = false); end
      # Returns a CLI option's value.
      def [](key); end
      # Sets a CLI option's value.
      def []=(key, value); end
    end

    # A small Hash implementation used internally to bridge C data structures with Ruby.
    #
    # This implementation would normally run slower when called and executed exclusively from within the Ruby layer,
    # as it is optimized for C callbacks and is used to bridge Ruby and C data structures.
    # 
    # To benchmark the C performance (excluding the Ruby overhead), call {MiniMap.cbench}.
    # 
    # It is also possible to benchmark the Ruby performance vs. the Ruby Hash using the `Iodine::Benchmark` module.
    module MiniMap
      # Returns the value associated with @key.
      def [](key); end
      # Sets the @value associated with @key and returns it.
      def []=(key, value); end
      # Runs @block for each key-value pair. Returns the number of times the block was executed.
      #
      #     m = Iodine::Base::MiniMap.new
      #     m[:go] = "home"
      #     m.each {|key, value| puts "#{key} => #{value}"}
      def each(&block); end
      # Returns the number of key-value pairs stored in the Hash.
      def count; end
      # Returns the maximum number of theoretical items the Hash can contain.
      #
      # Note that capacity increase will likely happen sooner and this number deals more with pre-allocated memory for the objects than with actual capacity.
      def capa; end
      # Reserves room in the Hash Map for at least the @capacity requested.
      #
      # See note in {capa}.
      def reserve(capacity); end
      # Benchmarks MiniMap v.s Hash performance when called from within the C layer.
      #
      # Numbers printed out are in time units. Lower is better.
      def self.cbench(number_of_items); end
    end
  end
  
  # class Error < StandardError; end
end

end # defined?(Iodine)
