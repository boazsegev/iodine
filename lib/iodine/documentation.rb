# unless defined?(Iodine)

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
# Methods for asynchronous execution include {run}, {async} and {run_after}.
#
# Methods for application wide pub/sub include {subscribe}, {unsubscribe} and {publish}. Connection specific pub/sub methods are documented in the {Iodine::Connection} class).
#
# Methods for TCP/IP, Unix Sockets and HTTP connections include {listen} and {Iodine::Connection.new}.
#
# Note that the HTTP server supports both TCP/IP and Unix Sockets as well as SSE / WebSockets extensions.
#
# Iodine doesn't call {Iodine::Utils.monkey_patch} automatically, but doing so will improve Rack's performance.
#
# Please read the {file:README.md} file for an introduction to Iodine.
#
# == TLS Backend Selection
#
# Iodine supports two TLS backends:
#
# 1. *Embedded TLS 1.3* (`:iodine`) - A lightweight, dependency-free TLS 1.3
#    implementation built into facil.io. Always available, no external libraries required.
#
# 2. *OpenSSL* (`:openssl`) - Uses the system's OpenSSL library. Requires
#    OpenSSL version 3.0 or higher. Used as default when available.
#
# === Runtime Selection (Recommended)
#
# Set the global default TLS backend at runtime:
#
#   # Switch to embedded TLS 1.3
#   Iodine::TLS.default = :iodine
#
#   # Switch to OpenSSL
#   Iodine::TLS.default = :openssl
#
# === Environment Variable
#
# Set before starting iodine:
#
#   IODINE_MTLS=1 iodine
#
# === Command Line
#
#   iodine -mtls
#
# === Compile-time Default
#
# Force embedded TLS as the compile-time default:
#
#   IODINE_USE_EMBEDDED_TLS=1 gem install iodine
#
# === Checking Available Backends
#
#   Iodine::TLS.default              # => :openssl or :iodine (current default)
#   Iodine::TLS.default = :iodine    # Set the default
#   Iodine::TLS::OPENSSL_AVAILABLE   # => true if OpenSSL is compiled in
#   Iodine::TLS::EMBEDDED_AVAILABLE  # => true (always available)
#   Iodine::TLS::SUPPORTED           # => true (always - at least embedded is available)
#
# *Note*: The embedded TLS 1.3 implementation has not been independently audited.
#
module Iodine
  # Returns the number of worker threads per worker process.
  #
  # Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  #
  # @return [Integer] the number of threads (defaults to -4 if not explicitly configured)
  def self.threads; end

  # Sets the number of worker threads per worker process.
  #
  # Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  #
  # @param threads [Integer] the number of threads
  # @return [Integer] the current CLI value for threads (may differ from input if set failed)
  # @raise [TypeError] if threads is not a Fixnum (only when called from master process with non-nil value)
  # @note Can only be set in the master process before starting the reactor.
  # @note If called from a worker process or with `nil`, logs an error but does not raise an exception.
  #   The return value will be the current (unchanged) CLI value.
  def self.threads=(threads); end

  # Returns the number of worker processes.
  #
  # Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  # A value of 0 means single-process mode (no forking).
  #
  # @return [Integer] the number of workers (defaults to -2 if not explicitly configured)
  def self.workers; end

  # Sets the number of worker processes.
  #
  # Negative values signify fractions of CPU core count (-1 ~= all CPU cores, -2 ~= half CPU cores, etc').
  # Set to 0 for single-process mode, or > 0 for cluster mode.
  #
  # @param workers [Integer] the number of worker processes
  # @return [Integer] the current CLI value for workers (may differ from input if set failed)
  # @raise [TypeError] if workers is not a Fixnum (only when called from master process with non-nil value)
  # @note Can only be set in the master process before starting the reactor.
  # @note If called from a worker process or with `nil`, logs an error but does not raise an exception.
  #   The return value will be the current (unchanged) CLI value.
  def self.workers=(workers); end

  # Starts the Iodine IO reactor.
  #
  # This is the main entry point that begins the event loop. It:
  # - Attaches the async thread pool
  # - Sets rack.multithread/rack.multiprocess environment values
  # - Logs startup information (version, workers, threads)
  # - Starts the IO event loop (blocking until stopped)
  #
  # @return [self]
  #
  # @example
  #   Iodine.listen(url: "http://0.0.0.0:3000", handler: MyApp)
  #   Iodine.start
  def self.start; end

  # Stops the current process' IO reactor.
  #
  # If this is a worker process, the process will exit and if Iodine is running
  # in cluster mode, a new worker will be spawned by the master process.
  #
  # @return [self]
  def self.stop; end

  # Returns `true` if this is the master (root) process.
  #
  # In cluster mode, the master process spawns and monitors worker processes.
  # In single-process mode, the process is both master and worker.
  #
  # @return [Boolean]
  def self.master?; end

  # Returns `true` if this is a worker process.
  #
  # Worker processes handle actual client connections and requests.
  # In single-process mode, the process is both master and worker.
  #
  # @return [Boolean]
  def self.worker?; end

  # Returns `true` if the IO reactor is currently running.
  #
  # @return [Boolean]
  def self.running?; end

  # Returns the current verbosity (logging) level.
  #
  # Log levels:
  # - 0: No logging
  # - 1: Fatal errors only
  # - 2: Errors and above
  # - 3: Warnings and above
  # - 4: Info and above (default)
  # - 5: Debug and above
  #
  # @return [Integer] the current log level (0-5)
  def self.verbosity; end

  # Sets the current verbosity (logging) level.
  #
  # @param logging_level [Integer] the log level (0-5)
  # @return [Integer] the new log level
  # @raise [TypeError] if logging_level is not a Fixnum
  #
  # @example
  #   Iodine.verbosity = 5  # Enable debug logging
  def self.verbosity=(logging_level); end

  # Returns the server's secret as a 64-byte binary string.
  #
  # The secret is used for cryptographic operations like session signing,
  # CSRF tokens, and other security-sensitive operations. It's derived
  # from the secret key set via `Iodine.secret=` or auto-generated.
  #
  # If no secret was previously set, Iodine will use the environment variable `SECRET` if available.
  # Otherwise, a random secret is produced.
  #
  # @return [String] 64-byte binary String containing the server secret
  #
  # @note It is recommended to set the secret as a Hex encoded String environment variable named `SECRET` before starting Ruby.
  def self.secret; end

  # Sets a new server secret based on the provided key.
  #
  # The key is hashed/expanded to produce the internal 512-bit secret.
  # This should be set before starting the server for consistent
  # cryptographic operations across restarts.
  #
  # @param secret_to_be_hashed [String] the secret key
  # @return [String] the new 64-byte server secret
  # @raise [TypeError] if secret_to_be_hashed is not a String
  def self.secret=(secret_to_be_hashed); end

  # Returns the current graceful shutdown timeout in milliseconds.
  #
  # During graceful shutdown, the server waits for active connections
  # to complete before forcing termination.
  #
  # @return [Integer] timeout in milliseconds
  def self.shutdown_timeout; end

  # Sets the graceful shutdown timeout in milliseconds.
  #
  # Maximum allowed value is 5 minutes (300,000 ms).
  #
  # @param milliseconds [Integer] timeout in milliseconds
  # @return [Integer] the new timeout value
  # @raise [TypeError] if milliseconds is not a Fixnum
  # @raise [RangeError] if milliseconds exceeds 5 minutes
  #
  # @example
  #   Iodine.shutdown_timeout = 30000  # 30 seconds
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
  # @param options [Hash] the options for the listener
  # @option options [String, nil] :url The address to listen at (or `nil`).
  # @option options [Object] :handler The handler (callback) object / NeoRack application / Rack application.
  # @option options [Symbol, String, nil] :service The service to be listening to (`http`, `https`, `ws`, `tcp`, `unix`). Can be provided as part of the `url`'s `scheme`.
  # @option options [Iodine::TLS, nil] :tls An Iodine::TLS instance.
  # @option options [Symbol, nil] :tls_io TLS backend selection: `:iodine` (embedded TLS 1.3), `:openssl` (OpenSSL >= 3.0), or `nil` (compile-time default).
  # @option options [String, nil] :public (HTTP only) Public folder for static file service.
  # @option options [Integer, nil] :max_age (HTTP only) The default maximum age for caching (when the etag header is provided).
  # @option options [Integer, nil] :max_header_size (HTTP only) The maximum size for HTTP headers.
  # @option options [Integer, nil] :max_line_len (HTTP only) The maximum size for a single HTTP header.
  # @option options [Integer, nil] :max_body_size (HTTP only) The maximum size for an HTTP payload.
  # @option options [Integer, nil] :max_msg_size (WebSocket only) The maximum size for a WebSocket message.
  # @option options [Integer, nil] :timeout (HTTP only) `keep-alive` timeout.
  # @option options [Integer, nil] :ping Connection timeout (WebSocket / `raw` / `tcp`).
  # @option options [Boolean, nil] :log (HTTP only) If `true`, logs `http` requests.
  # @return [Iodine::Listener] the listener object
  #
  # @note Either a `handler` or a `block` (Rack App) **must** be provided.
  #
  # @example HTTP server
  #   Iodine.listen(url: "http://0.0.0.0:3000", handler: MyApp)
  #
  # @example WebSocket server with TLS
  #   Iodine.listen(url: "wss://0.0.0.0:3000", handler: MyWSHandler, tls_io: :iodine)
  def self.listen(**options); end

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
  # @return [Proc] the block
  #
  # @note Code runs in both the parent and the child.
  #
  # @example
  #   Iodine.on_state(:start) { puts "Worker started!" }
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
  # @return [Proc] the block
  #
  # @note Blocking code MUST NOT be used in the block passed to this method,
  #   or Iodine's IO will hang while waiting for the blocking code to finish.
  # @note Method also accepts a method object if passed as a parameter.
  def self.run(&block); end

  # Runs a block of code in a worker thread (adds the code to the event queue).
  #
  # Always returns the block of code to executed (Proc object).
  #
  # Code will be executed only while Iodine is running (after {Iodine.start}).
  #
  # Code blocks that where scheduled to run before Iodine enters cluster mode will run on all child processes.
  #
  # The code may run in parallel with other calls to `run` or `async`, as it will run in one of the worker threads.
  #
  # @return [Proc] the block
  #
  # @note Method also accepts a method object if passed as a parameter.
  def self.async(&block); end

  # Runs the required block after the specified number of milliseconds have passed.
  #
  # The block will run in the main IO thread (adds the code to the event queue).
  #
  # Time is counted only once Iodine started running (using {Iodine.start}).
  #
  # @param milliseconds [Integer] the number of milliseconds between event repetitions.
  # @param repetitions [Integer] the number of event repetitions. Defaults to 1 (performed once). Set to 0 for never ending.
  # @return [Proc] a copy of the block object
  #
  # @note The event will repeat itself until the number of repetitions had been depleted or until the event returns `false`.
  #
  # @example Run once after 1 second
  #   Iodine.run_after(1000) { puts "1 second passed!" }
  #
  # @example Run every 5 seconds forever
  #   Iodine.run_after(5000, 0) { puts "tick" }
  def self.run_after(milliseconds, repetitions = 1, &block); end

  # Publishes a message to a combination of a channel (String) and filter (number).
  #
  # @param channel [String, nil] the channel name to publish to.
  # @param filter [Integer] the filter to publish to (Number < 32,767).
  # @param message [String] the actual message to publish.
  # @param engine [Iodine::PubSub::Engine, nil] the pub/sub engine to use (if not the default one).
  # @return [Boolean] `true` on success
  #
  # @example
  #   Iodine.publish("channel_name", 0, "payload")
  #   Iodine.publish(channel: "name", message: "payload")
  def self.publish(channel = nil, filter = 0, message = nil, engine = nil); end

  # Subscribes to a combination of a channel (String) and filter (number).
  #
  # @param channel [String, nil] the subscription's channel name.
  # @param filter [Integer] the subscription's filter (Number < 32,767).
  # @param since [Integer] replay cached messages since this timestamp in milliseconds
  #   (requires {Iodine::PubSub::History.cache} to be called first).
  # @param callback [Proc, nil] an **optional** object that answers to call and accepts a single argument (the message structure).
  # @return [Proc] the callback
  #
  # @note Either a proc or a block **must** be provided for global subscriptions.
  #
  # @example Basic subscription
  #   Iodine.subscribe("name") { |msg| puts msg.message }
  #   Iodine.subscribe(channel: "name", filter: 1) { |msg| puts msg.message }
  #
  # @example Subscription with history replay
  #   # First enable the memory cache
  #   Iodine::PubSub::History.cache
  #   # Subscribe and replay messages from the last 60 seconds
  #   since_ms = (Time.now.to_i - 60) * 1000
  #   Iodine.subscribe(channel: "chat", since: since_ms) { |msg| puts msg.message }
  def self.subscribe(channel = nil, filter = 0, since = 0, &callback); end

  # Un-Subscribes from a combination of a channel (String) and filter (number).
  #
  # @param channel [String, nil] the subscription's channel name.
  # @param filter [Integer] the subscription's filter (Number < 32,767).
  # @return [Boolean] `true` on success
  #
  # @example
  #   Iodine.unsubscribe("name")
  #   Iodine.unsubscribe(channel: "name", filter: 1)
  def self.unsubscribe(channel = nil, filter = 0); end

  # Adds an `on_http` implementation to `handler` that will route requests to their proper REST/CRUD callback.
  #
  # @param handler [Object] the handler object to modify
  # @return [Object] the modified handler
  #
  # @see Iodine::Connection::ResourceHandler
  def self.make_resource(handler); end

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
    #
    # @param json_string [String] the JSON string to parse
    # @return [Object] the parsed Ruby object
    def self.parse(json_string); end

    # Accepts a Ruby object and returns a JSON String.
    #
    # @param ruby_object [Object] the Ruby object to stringify
    # @return [String] the JSON string
    def self.stringify(ruby_object); end

    # Accepts a Ruby object and returns a prettier JSON String.
    #
    # @param ruby_object [Object] the Ruby object to stringify
    # @return [String] the formatted JSON string
    def self.beautify(ruby_object); end
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
    #        Iodine::Mustache.new(file = nil, template = nil, on_yaml = nil, &block)
    #
    # @param file [String] a file name for the mustache template.
    # @param template [String] the content of the mustache template - if `nil` or missing, `file` content will be loaded from disk.
    # @param on_yaml [Proc] (**optional**) accepts a YAML front-matter String.
    # @param block [Block] to be used as an implicit `on_yaml` (if missing).
    # @return [Iodine::Mustache] returns an Iodine::Mustache object with the provided template ready for rendering.
    #
    # @note Either the file or template argument (or both) must be provided.
    def initialize(file = nil, template = nil, on_yaml = nil, &block); end

    # Renders the template given at initialization with the provided context.
    #
    #        m.render(ctx)
    #
    # @param ctx [Hash] the top level context for the template data.
    # @return [String] returns a String containing the rendered template.
    def render(ctx = nil); end

    # Loads a template file and renders it into a String.
    #
    #        Iodine::Mustache.render(file = nil, template = nil, ctx = nil, on_yaml = nil)
    #
    # @param file [String] a file name for the mustache template.
    # @param template [String] the content of the mustache template - if `nil` or missing, `file` content will be loaded from disk.
    # @param ctx [Hash] the top level context for the template data.
    # @param on_yaml [Proc] (**optional**) accepts a YAML front-matter String.
    # @param block [Block] to be used as an implicit \p on_yaml (if missing).
    # @return [String] returns a String containing the rendered template.
    #
    # @note Either the file or template argument (or both) must be provided.
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
    #
    # @param str [String] the string to encode
    # @return [String] the encoded string
    def self.escape_path(str); end

    # Encodes a String in place using percent encoding (i.e., URI encoding).
    #
    # @param str [String] the string to encode in place
    # @return [String] the encoded string
    def self.escape_path!(str); end

    # Decodes percent encoding, including the `%uxxxx` JavaScript extension.
    #
    # @param str [String] the string to decode
    # @return [String] the decoded string
    def self.unescape_path(str); end

    # Decodes percent encoding in place, including the `%uxxxx` JavaScript.
    #
    # @param str [String] the string to decode in place
    # @return [String] the decoded string
    def self.unescape_path!(str); end

    # Encodes a String using percent encoding (i.e., URI encoding).
    #
    # @param str [String] the string to encode
    # @return [String] the encoded string
    def self.escape(str); end

    # Encodes a String using percent encoding (i.e., URI encoding).
    #
    # @param str [String] the string to encode in place
    # @return [String] the encoded string
    def self.escape!(str); end

    # Decodes percent encoding, including the `%uxxxx` JavaScript extension and converting `+` to spaces.
    #
    # @param str [String] the string to decode
    # @return [String] the decoded string
    def self.unescape(str); end

    # Decodes percent encoding in place, including the `%uxxxx` JavaScript extension and converting `+` to spaces.
    #
    # @param str [String] the string to decode in place
    # @return [String] the decoded string
    def self.unescape!(str); end

    # Escapes String using HTML escape encoding.
    #
    # @param str [String] the string to escape
    # @return [String] the escaped string
    #
    # @note This function may significantly increase the number of escaped characters when compared to the native implementation.
    def self.escape_html(str); end

    # Escapes String in place using HTML escape encoding.
    #
    # @param str [String] the string to escape in place
    # @return [String] the escaped string
    #
    # @note This function may significantly increase the number of escaped characters when compared to the native implementation.
    def self.escape_html!(str); end

    # Decodes an HTML escaped String.
    #
    # @param str [String] the string to decode
    # @return [String] the decoded string
    def self.unescape_html(str); end

    # Decodes an HTML escaped String in place.
    #
    # @param str [String] the string to decode in place
    # @return [String] the decoded string
    def self.unescape_html!(str); end

    # Takes a Time object and returns a String conforming to RFC 2109.
    #
    # @param time [Time] the time object
    # @return [String] the formatted time string
    def self.rfc2109(time); end

    # Takes a Time object and returns a String conforming to RFC 2822.
    #
    # @param time [Time] the time object
    # @return [String] the formatted time string
    def self.rfc2822(time); end

    # Takes a Time object and returns a String conforming to RFC 7231.
    #
    # @param time [Time] the time object
    # @return [String] the formatted time string
    def self.time2str(time); end

    # Securely compares two String objects to see if they are equal.
    #
    # Designed to be secure against timing attacks when both String objects are of the same length.
    #
    # @param a [String] the first string
    # @param b [String] the second string
    # @return [Boolean] `true` if equal, `false` otherwise
    def self.secure_compare(a, b); end

    # Adds the `Iodine::Utils` methods that are similar to the `Rack::Utils` module to the module(s) passed as arguments.
    #
    # If no modules were passed to the `monkey_patch` method, `Rack::Utils` will be monkey patched.
    #
    # @param to_patch [Module] the module(s) to patch
    # @return [self]
    def self.monkey_patch(to_patch = Rack::Utils); end

    # Returns a String with the requested length of random bytes.
    #
    # @param bytes [Integer] the number of random bytes (default: 16)
    # @return [String] a String containing the requested length of random bytes
    def self.random(bytes = 16); end

    # Generates a UUID using either random `secret` String, a random `info` String, both or none (a deterministic UUID), depending on the number parameters supplied.
    #
    # @param secret [String, nil] (**optional**) Adds salt to the UUID randomized number.
    # @param info [String, nil] (**optional**) Adds salt to the UUID randomized number.
    # @return [String] in UUID format
    def self.uuid(secret = nil, info = nil); end

    # Returns new Timed-One-Time-Password (TOTP), in Google format (6 digit Number with a 30 second validity).
    #
    # @param secret [String] the secret OTP for which the time based (T)OTP should be created.
    # @param offset [Integer] (**optional**) the number of "steps" backwards in time. Allows for testing of expired TOTPs.
    # @param interval [Integer] (**optional**) time window in seconds (default: 30).
    # @return [Integer] returns a 6 digit Number based on the secret provided and the current time.
    #
    # @example
    #   code = Iodine::Utils.totp(secret: my_secret)
    #   code = Iodine::Utils.totp(secret: my_secret, offset: -1)  # previous window
    def self.totp(secret:, offset: 0, interval: 30); end

    # Generates a new TOTP secret suitable for Google Authenticator.
    #
    # The secret is generated using cryptographically secure random bytes
    # and encoded in Base32 (uppercase, no padding) for compatibility with
    # authenticator apps.
    #
    # @param len [Integer] (**optional**) Length of the secret in bytes (default: 20, range: 10-64)
    # @return [String] Base32 encoded secret suitable for QR codes and authenticator apps
    #
    # @example
    #   secret = Iodine::Utils.totp_secret
    #   secret = Iodine::Utils.totp_secret(len: 32)
    def self.totp_secret(len: 20); end

    # Verifies a TOTP code against a secret with time window tolerance.
    #
    # The window parameter specifies how many intervals to check on either side
    # of the current time. For example, window: 1 checks current ± 1 interval.
    #
    # @param secret [String] the shared secret key (raw bytes or Base32 decoded)
    # @param code [Integer] the TOTP code to verify
    # @param window [Integer] (**optional**) number of intervals to check on each side (default: 1, range: 0-10)
    # @param interval [Integer] (**optional**) time window in seconds (default: 30)
    # @return [Boolean] `true` if the code is valid, `false` otherwise
    #
    # @example
    #   valid = Iodine::Utils.totp_verify(secret: my_secret, code: user_code)
    #   valid = Iodine::Utils.totp_verify(secret: my_secret, code: user_code, window: 2)
    def self.totp_verify(secret:, code:, window: 1, interval: 30); end

    # Generates 16 unique bytes of Poly1305-MAC, encoding them as a Base64 encoded String.
    #
    # @param secret [String] The secret used for the Hash based Message Authentication Code (MAC). Only the first 32 bytes are used.
    # @param msg [String] The message to authenticate.
    # @return [String] Base64 encoded String.
    def self.hmac128(secret, msg); end

    # Generates 20 unique bytes of SHA1-HMAC, encoding them as a Base64 encoded String.
    #
    # This is the equivalent of calling `OpenSSL::HMAC.base64digest "SHA1", secret, msg`
    #
    # @param secret [String] The secret used for the Hash based Message Authentication Code (MAC).
    # @param msg [String] The message to authenticate.
    # @return [String] Base64 encoded String.
    def self.hmac160(secret, msg); end

    # Generates 32 unique bytes of SHA256-HMAC, encoding them as a Base64 encoded String.
    #
    # This is the equivalent of calling `OpenSSL::HMAC.base64digest "SHA256", secret, msg`
    #
    # @param secret [String] The secret used for the Hash based Message Authentication Code (MAC).
    # @param msg [String] The message to authenticate.
    # @return [String] Base64 encoded String.
    def self.hmac256(secret, msg); end

    # Generates 64 unique bytes of SHA512-HMAC, encoding them as a Base64 encoded String.
    #
    # This is the equivalent of calling `OpenSSL::HMAC.base64digest "SHA512", secret, msg`
    #
    # @param secret [String] The secret used for the Hash based Message Authentication Code (MAC).
    # @param msg [String] The message to authenticate.
    # @return [String] Base64 encoded String.
    def self.hmac512(secret, msg); end

    # Computes SHA-256 hash of the data.
    #
    # @param data [String] the data to hash
    # @return [String] 32-byte binary hash
    def self.sha256(data); end

    # Computes SHA-512 hash of the data.
    #
    # @param data [String] the data to hash
    # @return [String] 64-byte binary hash
    def self.sha512(data); end

    # Computes SHA3-256 hash of the data.
    #
    # @param data [String] the data to hash
    # @return [String] 32-byte binary hash
    def self.sha3_256(data); end

    # Computes SHA3-512 hash of the data.
    #
    # @param data [String] the data to hash
    # @return [String] 64-byte binary hash
    def self.sha3_512(data); end

    # Computes BLAKE2b hash of the data.
    #
    # @param data [String] the data to hash
    # @param key [String, nil] optional key for keyed hashing
    # @param len [Integer] output length in bytes (1-64, default: 64)
    # @return [String] binary hash of specified length
    def self.blake2b(data, key: nil, len: 64); end

    # Computes BLAKE2s hash of the data.
    #
    # @param data [String] the data to hash
    # @param key [String, nil] optional key for keyed hashing
    # @param len [Integer] output length in bytes (1-32, default: 32)
    # @return [String] binary hash of specified length
    def self.blake2s(data, key: nil, len: 32); end

    # Computes SHA3-224 hash of the data.
    #
    # @param data [String] the data to hash
    # @return [String] 28-byte binary hash digest
    #
    # @example
    #   hash = Iodine::Utils.sha3_224("hello world")
    #   # => 28-byte binary string
    def self.sha3_224(data); end

    # Computes SHA3-384 hash of the data.
    #
    # @param data [String] the data to hash
    # @return [String] 48-byte binary hash digest
    #
    # @example
    #   hash = Iodine::Utils.sha3_384("hello world")
    #   # => 48-byte binary string
    def self.sha3_384(data); end

    # Computes SHAKE128 extendable-output function (XOF).
    #
    # SHAKE128 is a variable-length hash function from the SHA-3 family.
    # Unlike fixed-output hash functions, XOFs can produce output of any desired length.
    #
    # @param data [String] the data to hash
    # @param length [Integer] output length in bytes (default: 32)
    # @return [String] binary string of specified length
    # @raise [ArgumentError] if length is less than 1 or greater than 268435455
    #
    # @example
    #   # Default 32-byte output
    #   hash = Iodine::Utils.shake128("hello world")
    #
    #   # Custom length output
    #   hash = Iodine::Utils.shake128("hello world", length: 64)
    def self.shake128(data, length: 32); end

    # Computes SHAKE256 extendable-output function (XOF).
    #
    # SHAKE256 is a variable-length hash function from the SHA-3 family.
    # Unlike fixed-output hash functions, XOFs can produce output of any desired length.
    # SHAKE256 provides a higher security margin than SHAKE128.
    #
    # @param data [String] the data to hash
    # @param length [Integer] output length in bytes (default: 64)
    # @return [String] binary string of specified length
    # @raise [ArgumentError] if length is less than 1 or greater than 268435455
    #
    # @example
    #   # Default 64-byte output
    #   hash = Iodine::Utils.shake256("hello world")
    #
    #   # Custom length output
    #   hash = Iodine::Utils.shake256("hello world", length: 128)
    def self.shake256(data, length: 64); end

    # Computes SHA-1 hash of the data.
    #
    # @param data [String] the data to hash
    # @return [String] 20-byte binary hash digest
    #
    # @note SHA-1 is considered cryptographically weak and should not be used for
    #   security-sensitive applications. Use only for protocol compatibility
    #   (e.g., TOTP/HOTP, Git, WebSocket handshakes) where SHA-1 is required.
    #
    # @example
    #   hash = Iodine::Utils.sha1("hello world")
    #   # => 20-byte binary string
    def self.sha1(data); end

    # Computes a fast non-cryptographic hash using facil.io's Risky Hash.
    #
    # Risky Hash is optimized for speed and is suitable for hash tables,
    # checksums, data partitioning, and other non-security applications.
    #
    # @param data [String] the data to hash
    # @param seed [Integer] optional 64-bit seed value (default: 0)
    # @return [Integer] 64-bit hash value
    #
    # @note This hash function is NOT cryptographically secure. Do not use
    #   for security purposes such as password hashing or message authentication.
    #
    # @example
    #   # Basic usage
    #   hash = Iodine::Utils.risky_hash("hello world")
    #
    #   # With custom seed for different hash distributions
    #   hash = Iodine::Utils.risky_hash("hello world", seed: 12345)
    def self.risky_hash(data, seed: 0); end

    # Computes a 256-bit (32-byte) non-cryptographic hash using Risky Hash.
    #
    # This is a fast, high-quality hash function suitable for hash tables,
    # checksums, data deduplication, and other non-security applications.
    # The output is deterministic and consistent across platforms.
    #
    # @param data [String] the data to hash
    # @return [String] 32-byte binary hash digest
    #
    # @note This hash function is NOT cryptographically secure. Do not use
    #   for security purposes such as password hashing or message authentication.
    #
    # @example
    #   hash = Iodine::Utils.risky256("hello world")
    #   puts hash.bytesize  # => 32
    def self.risky256(data); end

    # Computes a 512-bit (64-byte) non-cryptographic hash using Risky Hash.
    #
    # This is a SHAKE-style extension of risky256: the first 256 bits of
    # the output are identical to risky256 (truncation-safe). The second
    # 256 bits extend the hash without increasing collision resistance.
    #
    # @param data [String] the data to hash
    # @return [String] 64-byte binary hash digest
    #
    # @note This hash function is NOT cryptographically secure. Do not use
    #   for security purposes such as password hashing or message authentication.
    #
    # @example
    #   hash = Iodine::Utils.risky512("hello world")
    #   puts hash.bytesize  # => 64
    #
    #   # First 32 bytes match risky256
    #   hash[0, 32] == Iodine::Utils.risky256("hello world")  # => true
    def self.risky512(data); end

    # Computes a 256-bit (32-byte) keyed HMAC using Risky Hash.
    #
    # Uses risky256 as the underlying hash with a 64-byte block size.
    # If key_len > 64, the key is hashed first with risky256.
    #
    # @param key [String] the secret key for authentication
    # @param data [String] the data to authenticate
    # @return [String] 32-byte binary HMAC digest
    #
    # @note While this provides message authentication, risky256 is not a
    #   cryptographic hash. For security-critical applications, use hmac256
    #   (SHA-256) or hmac512 (SHA-512) instead.
    #
    # @example
    #   mac = Iodine::Utils.risky256_hmac("secret-key", "message to authenticate")
    #   puts mac.bytesize  # => 32
    def self.risky256_hmac(key, data); end

    # Computes a 512-bit (64-byte) keyed HMAC using Risky Hash.
    #
    # Uses risky512 as the underlying hash with a 64-byte block size.
    # If key_len > 64, the key is hashed first with risky512.
    #
    # @param key [String] the secret key for authentication
    # @param data [String] the data to authenticate
    # @return [String] 64-byte binary HMAC digest
    #
    # @note While this provides message authentication, risky512 is not a
    #   cryptographic hash. For security-critical applications, use hmac256
    #   (SHA-256) or hmac512 (SHA-512) instead.
    #
    # @example
    #   mac = Iodine::Utils.risky512_hmac("secret-key", "message to authenticate")
    #   puts mac.bytesize  # => 64
    def self.risky512_hmac(key, data); end

    # Generates cryptographically secure random bytes using the system CSPRNG.
    #
    # Uses arc4random_buf on BSD/macOS or /dev/urandom on Linux to generate
    # random bytes suitable for cryptographic key generation, nonces, and
    # other security-sensitive applications.
    #
    # @param bytes [Integer] number of random bytes to generate (default: 32)
    # @return [String] binary string containing the requested random bytes
    # @raise [RangeError] if bytes count is out of range
    # @raise [RuntimeError] if the CSPRNG fails to generate random bytes
    #
    # @example
    #   # Generate a 32-byte key (default)
    #   key = Iodine::Utils.secure_random
    #
    #   # Generate a 16-byte nonce
    #   nonce = Iodine::Utils.secure_random(bytes: 16)
    #
    #   # Generate a 64-byte seed
    #   seed = Iodine::Utils.secure_random(bytes: 64)
    def self.secure_random(bytes: 32); end
  end

  #######################

  # This is the namespace for all pub/sub related classes and constants.
  module PubSub
    # Returns the current default PubSub engine.
    #
    # @return [Iodine::PubSub::Engine] the current default engine
    def self.default; end

    # Sets the default PubSub engine for all publish operations.
    #
    # @param engine [Iodine::PubSub::Engine, nil] the engine to set as default (or nil for CLUSTER)
    # @return [Iodine::PubSub::Engine] the new default engine
    #
    # @example
    #   Iodine::PubSub.default = Iodine::PubSub::Engine::Redis.new("redis://localhost:6379/")
    def self.default=(engine); end

    # Iodine::PubSub::Message class instances are passed to subscription callbacks.
    #
    # This allows subscription callbacks to get information about the pub/sub event.
    class Message
      # Returns the event's `id` property - a (mostly) random numerical value.
      # @return [Integer]
      def id; end

      # Returns the event's `channel` property - the event's target channel.
      # @return [String, nil]
      def channel; end
      alias event channel

      # Returns the event's `filter` property - the event's numerical channel filter.
      # @return [Integer]
      def filter; end

      # Returns the event's `message` property - the event's payload (same as {to_s}).
      # @return [String]
      def message; end
      alias msg message
      alias data message

      # Returns the event's `published` property - the event's published timestamp in milliseconds since epoch.
      # @return [Integer]
      def published; end

      # Returns the event's `message` property - the event's payload (same as {message}).
      # @return [String]
      def to_s; end

      # Sets the event's `id` property.
      # @param value [Integer]
      def id=(value); end

      # Sets the event's `channel` property.
      # @param value [String]
      def channel=(value); end

      # Sets the event's `filter` property.
      # @param value [Integer]
      def filter=(value); end

      # Sets the event's `message` property.
      # @param value [String]
      def message=(value); end

      # Sets the event's `published` property.
      # @param value [Integer]
      def published=(value); end
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
      #
      # @param msg [Iodine::PubSub::Message] the message to publish
      def publish(msg); end

      # Please OVERRIDE(!) – called by iodine when a named channel was subscribed to.
      #
      # @param channel [String] the channel name
      def subscribe(channel); end

      # Please OVERRIDE(!) – called by iodine when a pattern channel was subscribed to.
      #
      # @param pattern [String] the pattern
      def psubscribe(pattern); end

      # Please OVERRIDE(!) – called by iodine when a named channel was unsubscribed to.
      #
      # @param channel [String] the channel name
      def unsubscribe(channel); end

      # Please OVERRIDE(!) – called by iodine when a pattern channel was unsubscribed to.
      #
      # @param pattern [String] the pattern
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

      # Redis Pub/Sub engine for distributed messaging across multiple server instances.
      #
      # Provides both Pub/Sub functionality and direct Redis command execution.
      #
      # @example Basic usage
      #   redis = Iodine::PubSub::Engine::Redis.new("redis://localhost:6379/", ping: 50)
      #   Iodine::PubSub.default = redis
      #
      # @example With authentication
      #   redis = Iodine::PubSub::Engine::Redis.new("redis://user:password@host:6379/0")
      #
      # @example Sending Redis commands
      #   redis.cmd("SET", "key", "value") { |result| puts result }
      #   redis.cmd("GET", "key") { |value| puts value }
      #   redis.cmd("KEYS", "*") { |keys| p keys }
      class Redis < Engine
        # Creates a new Redis Pub/Sub engine.
        #
        # @param url [String] Redis server URL (e.g., "redis://localhost:6379/")
        # @param ping [Integer] Ping interval in seconds (0-255, default: 300)
        # @return [Iodine::PubSub::Engine::Redis]
        #
        # URL formats supported:
        # - "redis://host:port"
        # - "redis://user:password@host:port/db"
        # - "host:port"
        # - "host" (default port 6379)
        #
        # @example
        #   redis = Iodine::PubSub::Engine::Redis.new("redis://localhost:6379/")
        #   redis = Iodine::PubSub::Engine::Redis.new("redis://secret@host:6379/", ping: 60)
        def initialize(url, ping: 0); end

        # Sends a Redis command and optionally receives the response via callback.
        #
        # @param args [Array<String, Integer, Float, Symbol, Boolean, nil>] Command and arguments
        # @yield [result] Called with the Redis response
        # @yieldparam result [Object] The parsed Redis response
        # @return [Boolean] true on success, false on error
        #
        # @example
        #   redis.cmd("SET", "key", "value") { |r| puts "SET result: #{r}" }
        #   redis.cmd("GET", "key") { |value| puts "Value: #{value}" }
        #   redis.cmd("KEYS", "*") { |keys| p keys }
        #   redis.cmd("INCR", "counter") { |new_val| puts new_val }
        #
        # @note Do NOT use SUBSCRIBE/PSUBSCRIBE/UNSUBSCRIBE/PUNSUBSCRIBE commands.
        #       These are handled internally by the pub/sub system.
        def cmd(*args, &block); end
      end
    end

    # Iodine::PubSub::History module provides message history and replay support.
    #
    # When history is enabled, published messages are cached in memory (up to a
    # configurable size limit). Subscribers can then request replay of missed
    # messages by providing a `since` timestamp when subscribing.
    #
    # @example Enable memory cache with default 256MB limit
    #   Iodine::PubSub::History.cache
    #
    # @example Enable memory cache with custom size
    #   Iodine::PubSub::History.cache(size_limit: 128 * 1024 * 1024)  # 128MB
    #
    # @example Subscribe with history replay
    #   # Get messages from the last 60 seconds
    #   since_ms = (Time.now.to_i - 60) * 1000
    #   Iodine.subscribe(channel: "chat", since: since_ms) do |msg|
    #     puts "Message: #{msg.message}"
    #   end
    module History
      # Enables the built-in in-memory history cache.
      #
      # The memory cache has the highest priority (255) so it will be tried
      # first for replay requests, providing the fastest possible replay.
      #
      # @param size_limit [Integer] Maximum cache size in bytes (default: 256MB)
      # @return [Boolean] true on success
      #
      # @example
      #   Iodine::PubSub::History.cache(size_limit: 128 * 1024 * 1024)
      def self.cache(size_limit: 256 * 1024 * 1024); end

      # Returns true if the built-in memory cache is enabled.
      #
      # @return [Boolean]
      def self.cache?; end

      # Base class for custom history managers.
      #
      # Subclass this to implement custom history storage backends (e.g., Redis,
      # database, file-based storage).
      #
      # @example Custom history manager
      #   class MyHistoryManager < Iodine::PubSub::History::Manager
      #     def push(message)
      #       # Store message in your backend
      #       @storage[message.channel] ||= []
      #       @storage[message.channel] << message
      #       true
      #     end
      #
      #     def replay(channel:, filter:, since:)
      #       # Return array of messages since timestamp
      #       (@storage[channel] || []).select { |m| m.published >= since }
      #     end
      #
      #     def oldest(channel:, filter:)
      #       # Return oldest available timestamp
      #       @storage[channel]&.first&.published || (2**64 - 1)
      #     end
      #   end
      class Manager
        # Creates and attaches a new history manager.
        #
        # @param priority [Integer] Manager priority (0-255, default: 128).
        #   Higher priority managers are tried first for replay requests.
        # @return [self]
        def initialize(priority: 128); end

        # Detaches the history manager from the pubsub system.
        #
        # @return [self]
        def detach; end

        # Returns true if the manager is attached.
        #
        # @return [Boolean]
        def attached?; end

        # Override this to store a message in history.
        #
        # @param message [Iodine::PubSub::Message] the message to store
        # @return [Boolean] true on success, false on error
        def push(message); end

        # Override this to replay messages since a timestamp.
        #
        # @param channel [String, nil] the channel name
        # @param filter [Integer] the filter value
        # @param since [Integer] timestamp in milliseconds since epoch
        # @return [Array<Iodine::PubSub::Message>] array of messages to replay
        def replay(channel:, filter:, since:); end

        # Override this to get the oldest available timestamp for a channel.
        #
        # @param channel [String, nil] the channel name
        # @param filter [Integer] the filter value
        # @return [Integer] oldest timestamp in milliseconds, or 2^64-1 if no history
        def oldest(channel:, filter:); end

        # Called when the manager is detached for cleanup.
        def on_cleanup; end
      end
    end
  end

  #######################

  # This is a listener object, returned from {Iodine.listen}.
  #
  # **Note**: calling {Iodine::Listener.new} would raise an exception. Use {Iodine.listen} instead.
  class Listener
    # Sets the handler for new connections.
    #
    # @param url [String, nil] should be `nil` unless listening for HTTP connections, in which case `url`, if set, will be used for routing the `path` part of the URL requested to the specified handler.
    # @param handler [Object] should contain a valid {Iodine::Connection::Handler} for incoming connections.
    # @return [self]
    #
    # Routing with HTTP `url` values will follow the following rules:
    #
    # - Routing follows the best-match approach regardless of ordering. If the best match is the root URL `'/'`, the default handler will be used.
    #
    # - Partial URL matches are only valid if the `/` character is the one following the partial match. For example: setting `"/user"` will match `"/user"` and all `"/user/*"` paths but not `"/user*"`
    #
    # - The {Iodine::Connection#path} property will NOT contain the removed path prefix, but will always contain and start with the `/` character. For example: matching the path `"/user/new"` with the route for `"/user"` will set the {Iodine::Connection#path} property to `'/new'`.
    #
    # @note When `handler` is `false` instead of `nil`, the handler for the route will be replace with the default handler. However, the {Iodine::Connection#path} property for the HTTP handler will behave as if a route match has occurred, removing the route's prefix.
    def map(url = nil, handler = nil); end
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
  # - Pub/Sub related methods include the {#subscribe}, {#unsubscribe}, and {#publish} methods.
  #
  # - Connection specific data can be stored / accessed using {#[]}, {#[]=}, {#each}, {#store_count}, {#store_capa}, {#store_reserve} methods.
  #
  class Connection
    # Creates a new outgoing client connection.
    #
    # @param url [String] The URL to connect to (required). Supports `tcp://`, `tcps://`, `http://`, `https://`, `ws://`, `wss://` schemes.
    # @param handler [Object] The handler object implementing connection callbacks (see {Handler}).
    # @param tls [Iodine::TLS, Boolean] TLS context or `true` for auto-generated TLS.
    # @param tls_io [Symbol, nil] TLS backend selection: `:iodine` (embedded TLS 1.3), `:openssl` (OpenSSL >= 3.0), or `nil` (compile-time default). Only effective when TLS is enabled.
    # @param headers [Hash] (HTTP/WebSocket only) Request headers to send.
    # @param cookies [Hash] (HTTP/WebSocket only) Cookies to send.
    # @param body [String] (HTTP only) Request body.
    # @param method [String] (HTTP only) HTTP method (defaults to GET).
    # @return [Iodine::Connection] The new connection object.
    #
    # @example WebSocket client with embedded TLS
    #   Iodine::Connection.new("wss://example.com/socket", handler: MyHandler, tls_io: :iodine)
    #
    # @example WebSocket client with OpenSSL
    #   Iodine::Connection.new("wss://example.com/socket", handler: MyHandler, tls_io: :openssl)
    def initialize(url, handler: nil, tls: nil, tls_io: nil, headers: nil, cookies: nil, body: nil, method: nil); end

    # Unless `env` is manually set, Iodine will attempt to create a default `env` object that will follow the Rack Server specification for HTTP connections.
    #
    # Otherwise, the `env` attribute is usually a Hash object set by the client and may be used as an alternative to the Connection data storage (see {#[]} and {#[]=}).
    # @return [Hash]
    attr_accessor :env

    # The `handler` attribute (see {Iodine::Connection::Handler}) - this may be the Rack application or a [NeoRack](https://github.com/boazsegev/neorack) application.
    # @return [Object]
    attr_accessor :handler

    # For HTTP connections, the `status` value is the HTTP status value that will be sent in the response.
    # @return [Integer]
    attr_accessor :status

    # For HTTP connections, the `method` value is the HTTP method value received by the client.
    # @return [String]
    attr_accessor :method

    # For HTTP connections, the `path` value is the HTTP URL's path String received by the client. When routing, this does **NOT** include the route's prefix (see {Iodine::Listener#map}).
    # @return [String]
    attr_accessor :path

    # For HTTP connections, the `opath` value is the HTTP URL's original path String received by the client.
    # @return [String]
    attr_accessor :opath

    # For HTTP connections, the `query` value is the HTTP URL's query String (if received by the client).
    # @return [String, nil]
    attr_accessor :query

    # For HTTP connections, the `version` String value stated by the HTTP client.
    # @return [String]
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
    # @return [self]
    attr_accessor :headers

    # (HTTP Only) Returns the length of the `body` payload.
    # @return [Integer]
    attr_reader :length

    # @!group Key-Data Store

    # Returns a the value of a key previously set.
    #
    # If the Connection is an HTTP connection and `key` is a Header String, the value of the header (if received) will be returned.
    #
    # If no value was set for the key (or no Header received), `nil` will be returned.
    #
    # @param key [Object] the key to look up
    # @return [Object, nil] the value or nil
    def [](key); end

    # Sets a key value pair.
    #
    # @param key [Object] the key
    # @param value [Object] the value
    # @return [Object] the value
    def []=(key, value); end

    # Iterates over the stored key value pairs.
    #
    # If the Connection is an HTTP connection, `each` will also iterate over any header previously accessed.
    #
    # @yield [key, value] the key-value pairs
    # @return [self]
    def each(&block); end

    # Returns the number of key value pairs stored.
    # @return [Integer]
    def store_count; end

    # Returns the theoretical capacity of the key value pair storage map.
    # @return [Integer]
    def store_capa; end

    # Reserved a theoretical storage capacity in the key value pair storage map.
    #
    # @param number_of_items [Integer] the number of items to reserve
    # @return [self]
    #
    # @note The capacity is theoretical only and some extra memory is required to allow for possible (partial) hash collisions.
    def store_reserve(number_of_items); end

    # @!group Connection State

    # Returns `true` if the connection appears to be open (no known issues).
    #
    # (HTTP Only) Returns `true` only if upgraded and open (WebSocket is open).
    # @return [Boolean]
    def open?; end

    # (HTTP Only) Returns `false` if headers can still be sent. Otherwise returns `true`.
    # @return [Boolean]
    def headers_sent?; end

    # Returns `true` only if the Connection object is still valid (data can be sent).
    # @return [Boolean]
    def valid?; end

    # Returns true only of the connection is a WebSocket connection.
    # @return [Boolean]
    def websocket?; end

    # Returns true only of the connection is an SSE connection.
    # @return [Boolean]
    def sse?; end

    # Returns true only of the connection is an HTTP connection (not upgraded).
    # @return [Boolean]
    def http?; end

    # Returns the number of bytes that need to be sent before the next `on_drained` callback is called.
    # @return [Integer]
    def pending; end

    # Schedules the connection to be closed.
    # @return [self]
    def close; end

    # @!group Writing to the Connection

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
    #
    # @param name [String] the header name (lowercase)
    # @param values [String, Array<String>, nil] the header value(s)
    # @return [Boolean] `true` on success, `false` if headers already sent
    def write_header(name, values); end

    # Writes data to the connection asynchronously.
    #
    # If `data` **should* be a String. However, if `data` is not a String, Iodine will attempt to convert it to a JSON String, allowing Hashes, Arrays and other native Ruby objects to be sent over the wire. Note that `data` **must** be a native Ruby Object, or it could not be converted to a JSON String.
    #
    # For WebSocket connections: if `data` is a UTF-8 encoded String, message will be sent as `text`, otherwise message will be sent as `binary`.
    #
    # For SSE connections: this is similar to calling `write_sse(nil, nil, data)`.
    #
    # @param data [String, Object] the data to write
    # @return [Boolean] `false` on error / fail and `true` if the event was scheduled to be sent to the client
    def write(data); end

    # Writes data to EventSource client connections asynchronously.
    #
    # The `id` and `event` elements **must** be UTF-8 encoded Strings.
    #
    # If `data` **should* be a String. However, if `data` is not a String, Iodine will attempt to convert it to a JSON String, allowing Hashes, Arrays and other native Ruby objects to be sent over the wire. Note that `data` **must** be a native Ruby Object, or it could not be converted to a JSON String.
    #
    # An empty `data` String or a missing `data` argument will cause `write_sse` to return false.
    #
    # @param id [String, nil] the event ID
    # @param event [String, nil] the event type
    # @param data [String, Object] the event data
    # @return [Boolean] `false` on error / fail and `true` if the event was scheduled to be sent to the client
    def write_sse(id, event, data); end

    # Closes the connection after writing `data`, or (HTTP Only) finishes the HTTP response.
    #
    # @param data [String, nil] optional data to write before closing
    # @return [self]
    #
    # @note For HTTP connections the `"content-length"` header is automatically written if no previous calls to `write` were called.
    def finish(data = nil); end

    # @!group Reading the HTTP Body / Payload

    # (HTTP Only) Returns an ASCII-8 String with the next line in the Body (same as {::IO#gets} where only `limit` is allowed).
    #
    # @param limit [Integer, nil] maximum number of bytes to read
    # @return [String, nil] the line or nil if at end
    def gets(limit = nil); end

    # (HTTP Only) Returns an ASCII-8 String with (part of) the Body (see {::IO#read}).
    #
    # @param maxlen [Integer, nil] maximum number of bytes to read
    # @param out_string [String, nil] optional buffer to read into
    # @return [String, nil] the data or nil if at end
    def read(maxlen = nil, out_string = nil); end

    # (HTTP Only) Sets the body's new read position. Negative values start at the end of the body. `nil` returns current position.
    #
    # @param pos [Integer, nil] the new position
    # @return [Integer] the current position
    def seek(pos = nil); end

    # @!group HTTP Cookies

    # (HTTP Only) Returns an ASCII-8 String with the value of the named cookie.
    #
    # @param name [String, Symbol] the cookie name
    # @return [String, nil] the cookie value or nil
    def cookie(name); end

    # (HTTP Only) Sets the value of the named cookie and returns `true` upon success.
    #
    # If `headers_sent?` returns `true`, calling this method may raise an exceptions.
    #
    # If `value` is `nil`, the cookie will be deleted. If `:max_age` is 0 (default), cookie will be session cookie and will be deleted by the browser at its discretion.
    #
    # This should behave similar to calling `write_header`, except that the cookie can be read using the {#cookie} method.
    #
    # @param name [String, Symbol] the cookie name
    # @param value [String, nil] the cookie value (nil to delete)
    # @param max_age [Integer] max age in seconds (0 for session cookie)
    # @param domain [String, nil] the cookie domain
    # @param path [String, nil] the cookie path
    # @param same_site [Symbol, nil] `:default`, `:none`, `:lax`, or `:strict`
    # @param secure [Boolean] if true, cookie only sent over HTTPS
    # @param http_only [Boolean] if true, cookie not accessible via JavaScript
    # @param partitioned [Boolean] if true, cookie is partitioned
    # @return [Boolean] `true` on success
    #
    # @example
    #   set_cookie(name: "MyCookie", value: "My non-secret data", domain: "localhost", max_age: 1_728_000)
    #
    # @see https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Set-Cookie
    def set_cookie(name, value = nil, max_age = 0, domain = nil, path = nil, same_site = nil, secure = false,
                   http_only = false, partitioned = false)
    end

    # Calls `block` for each name-value cookie pair.
    #
    # @yield [name, value] the cookie name and value
    # @return [self]
    def each_cookie(&block); end

    # @!group Pub/Sub

    # Publishes a message to a combination of a channel (String) and filter (number).
    #
    # @param channel [String, nil] the channel name to publish to
    # @param filter [Integer] the filter to publish to (Number < 32,767)
    # @param message [String] the actual message to publish
    # @param engine [Iodine::PubSub::Engine, nil] the pub/sub engine to use (if not the default one)
    # @return [Boolean] `true` on success
    #
    # @example
    #   Iodine::Connection.publish("channel_name", 0, "payload")
    #   Iodine::Connection.publish(channel: "name", message: "payload")
    def self.publish(channel = nil, filter = 0, message = nil, engine = nil); end

    # Publishes a message to a combination of a channel (String) and filter (number).
    #
    # **Note**: events published by a specific client are sent to "**everyone else**" and ignored by the client itself.
    #
    # **Note**: if `engine` was specified and it isn't an Iodine engine, message **may** be delivered to the client calling `publish` as well.
    #
    # To publish events to everyone, including the client itself, use the global {Iodine.publish} method or the {.publish} class method.
    #
    # @param channel [String, nil] the channel name to publish to
    # @param filter [Integer] the filter to publish to (Number < 32,767)
    # @param message [String] the actual message to publish
    # @param engine [Iodine::PubSub::Engine, nil] the pub/sub engine to use (if not the default one)
    # @return [Boolean] `true` on success
    #
    # @example
    #   client.publish("channel_name", 0, "payload")
    #   client.publish(channel: "name", message: "payload")
    def publish(channel = nil, filter = 0, message = nil, engine = nil); end

    # Subscribes to a combination of a channel (String) and filter (number).
    #
    # @param channel [String, nil] the subscription's channel name
    # @param filter [Integer] the subscription's filter (Number < 32,767)
    # @param since [Integer] replay cached messages since this timestamp in milliseconds
    #   (requires {Iodine::PubSub::History.cache} to be called first)
    # @param callback [Proc, nil] an **optional** object that answers to call and accepts a single argument (the message structure)
    # @return [Proc, nil] the callback
    #
    # @note Either a proc or a block MUST be provided for global subscriptions.
    #
    # @example Basic subscription
    #   client.subscribe("name") { |msg| puts msg.message }
    #   client.subscribe(channel: "name", filter: 1) { |msg| puts msg.message }
    #
    # @example Subscription with history replay
    #   since_ms = (Time.now.to_i - 60) * 1000
    #   client.subscribe(channel: "chat", since: since_ms) { |msg| puts msg.message }
    def subscribe(channel = nil, filter = 0, since = 0, &callback); end

    # Subscribes to a combination of a channel (String) and filter (number).
    #
    # @param channel [String, nil] the subscription's channel name
    # @param filter [Integer] the subscription's filter (Number < 32,767)
    # @param since [Integer] replay cached messages since this timestamp in milliseconds
    #   (requires {Iodine::PubSub::History.cache} to be called first)
    # @param callback [Proc, nil] an **optional** object that answers to call and accepts a single argument (the message structure)
    # @return [Proc, nil] the callback
    #
    # @note Either a proc or a block MUST be provided for global subscriptions.
    #
    # @example
    #   Iodine::Connection.subscribe("name") { |msg| puts msg.message }
    def self.subscribe(channel = nil, filter = 0, since = 0, &callback); end

    # Un-Subscribes from a combination of a channel (String) and filter (number).
    #
    # @param channel [String, nil] the subscription's channel name
    # @param filter [Integer] the subscription's filter (Number < 32,767)
    # @return [Boolean] `true` on success
    #
    # @example
    #   client.unsubscribe("name")
    #   client.unsubscribe(channel: "name", filter: 1)
    def unsubscribe(channel = nil, filter = 0); end

    # Un-Subscribes from a combination of a channel (String) and filter (number).
    #
    # @param channel [String, nil] the subscription's channel name
    # @param filter [Integer] the subscription's filter (Number < 32,767)
    # @return [Boolean] `true` on success
    #
    # @example
    #   Iodine::Connection.unsubscribe("name")
    def self.unsubscribe(channel = nil, filter = 0); end

    # @!group Misc

    # Returns the raw peer IP address.
    # @return [String]
    def peer_addr; end

    # Returns the published peer address (may differ with proxies).
    # @return [String]
    def from; end

    # Hijacks the connection from the Server and returns an IO object.
    #
    # This method MAY be used to implement a full hijack when providing compatibility with the Rack specification.
    #
    # @return [IO] for current underlying socket connection
    def rack_hijack; end

    #######################

    # This is an example handler for documentation purposes **only**.
    #
    # This module is designed to help you author your own Handler for {Iodine.listen} and {Iodine::Connection.new}.
    #
    # A handler implementation is expected to implement at least one of the callbacks defined for this example Handler. Any missing callbacks will be defined by Iodine using smart defaults (where possible).
    #
    # **Note**: This module isn't implemented by Iodine, but is for documentation and guidance only.
    module Handler
      # @!group Callbacks

      # Used for [NeoRack](https://github.com/boazsegev/neorack) application authoring, when receiving HTTP requests.
      #
      # [NeoRack](https://github.com/boazsegev/neorack) allows both Async and CGI style Web Applications, making it easier to stream data and do more with a single thread.
      #
      # @param event [Iodine::Connection] the connection/event object
      def self.on_http(event); end

      # Used for [NeoRack](https://github.com/boazsegev/neorack) application authoring, when an HTTP request handling had finished.
      #
      # @param event [Iodine::Connection] the connection/event object
      def self.on_finish(event); end

      # Used for [Rack](https://github.com/rack/rack) application authoring, when receiving HTTP requests.
      #
      # [Rack](https://github.com/rack/rack) is the classical server interface and it is supported by Iodine with minimal extensions.
      #
      # @param env [Hash] the Rack environment hash
      # @return [Array] the Rack response [status, headers, body]
      def self.call(env); end

      # When present, a missing `on_authenticate_sse` or missing `on_authenticate_websocket` will route to this callback, allowing authentication logic to be merged in one method.
      #
      # **Must** return `true` for the connection to be allowed. **Any other return value will cause the connection to be refused** (and disconnected).
      #
      # By default, if `on_message` or `on_open` are defined, than `on_authenticate` returns `true`. Otherwise, `on_authenticate` returns `false`.
      #
      # @param connection [Iodine::Connection] the connection object
      # @return [Boolean] `true` to allow, `false` to deny
      def self.on_authenticate(connection); end

      # Called when an Event Source (SSE) request is detected, to authenticate and possibly set the identity of the user.
      #
      # By default, if `on_open` is defined, than `on_authenticate_sse` returns `true`. Otherwise, `on_authenticate` returns `false`.
      #
      # @param connection [Iodine::Connection] the connection object
      # @return [Boolean] `true` to allow, `false` to deny
      def self.on_authenticate_sse(connection); end

      # Called when a WebSocket Upgrade request is detected, to authenticate and possibly set the identity of the user.
      #
      # By default, if `on_message` or `on_open` are defined, than `on_authenticate_websocket` returns `true`. Otherwise, `on_authenticate` returns `false`.
      #
      # @param connection [Iodine::Connection] the connection object
      # @return [Boolean] `true` to allow, `false` to deny
      def self.on_authenticate_websocket(connection); end

      # Called when a long-running connection opens. This will be called for Event Source, WebSocket, TCP/IP and Raw Unix Socket connections.
      #
      # @param connection [Iodine::Connection] the connection object
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
      #
      # @param connection [Iodine::Connection] the connection object
      # @param data [String] the received data
      def self.on_message(connection, data); end

      # Called when all calls to {Iodine::Connection#write} have been handled and the outgoing buffer is now empty.
      #
      # @param connection [Iodine::Connection] the connection object
      def self.on_drained(connection); end

      # Called when timeout has been reached for a WebSocket / TCP/IP / Raw Unix Socket connection.
      #
      # @param connection [Iodine::Connection] the connection object
      def self.on_timeout(connection); end

      # Called when the worker that manages this connection (or the root process, in non-cluster mode) starts shutting down.
      #
      # @param connection [Iodine::Connection] the connection object
      def self.on_shutdown(connection); end

      # Called when the connection was closed (WebSocket / TCP/IP / Raw Unix Socket).
      #
      # @param connection [Iodine::Connection] the connection object
      def self.on_close(connection); end

      # Called when an Event Source (SSE) event has been received (when acting as an Event Source client).
      #
      # @param connection [Iodine::Connection] the connection object
      # @param message [Iodine::PubSub::Message] the SSE message
      def self.on_eventsource(connection, message); end

      # Called an Event Source (SSE) client send a re-connection request with the ID of the last message received.
      #
      # @param connection [Iodine::Connection] the connection object
      # @param last_message_id [String] the last message ID
      def self.on_eventsource_reconnect(connection, last_message_id); end
    end

    # This is an example REST/CRUD resource handler for documentation purposes **only**.
    #
    # This module is designed to help you author your own Resource Handler for {Iodine.make_resource}.
    #
    # A resource handler implementation is expected to implement all the callbacks. Any missing callbacks will result in a 404 error.
    #
    # **Note**: This module isn't implemented by Iodine, it is for documentation and guidance only.
    module ResourceHandler
      # @!group Callbacks

      # Called for `"GET /"`.
      # @param e [Iodine::Connection] the connection object
      def self.index(e) = e.finish('Show Index')

      # Called for `"GET /(id)"`, where `id` can be assumed to be: `e.path[1..-1]`.
      # @param e [Iodine::Connection] the connection object
      def self.show(e) = e.finish("Show Item: #{e.path[1..-1]}")

      # Called for `"GET /new"`, expecting a response containing a form for posting a new element.
      # @param e [Iodine::Connection] the connection object
      def self.new(e) = e.finish('New Item Form.')

      # Called for `"GET /(id)/edit"`, expecting a response containing a form for editing the existing element with `id` being `e.path.split('/')[1]`.
      # @param e [Iodine::Connection] the connection object
      def self.edit(e) = e.finish("Edit Item: #{e.path[1..-1]}")

      # Called for `"PATCH /"`, `"PUT /"`, `"POST /"`, `"PATCH /new"`, `"PUT /new"`, `"POST /new"` (`new` is optional).
      # @param e [Iodine::Connection] the connection object
      def self.create(e) = e.finish('Create a new Item.')

      # Called for `"PATCH /(id)"`, `"PUT /(id)"`, `"POST /(id)"`, where `id` can be assumed to be: `e.path[1..-1]`.
      # @param e [Iodine::Connection] the connection object
      def self.update(e) = e.finish("Update Item: #{e.path[1..-1]}")

      # Called for `"DELETE /(id)"`, where `id` can be assumed to be: `e.path[1..-1]`.
      # @param e [Iodine::Connection] the connection object
      def self.delete(e) = e.finish("Delete Item: #{e.path[1..-1]}")
    end
  end

  #######################

  # Used to setup a TLS contexts for connections (incoming / outgoing).
  #
  # == TLS Backend Constants
  #
  # The following constants provide information about available TLS backends:
  #
  # - `Iodine::TLS::SUPPORTED` - Always `true` (embedded TLS always available).
  # - `Iodine::TLS::OPENSSL_AVAILABLE` - Returns `true` if OpenSSL backend is compiled in.
  # - `Iodine::TLS::EMBEDDED_AVAILABLE` - Returns `true` (embedded TLS 1.3 is always available).
  #
  # @example Checking available backends
  #   puts "Default TLS backend: #{Iodine::TLS.default}"
  #   puts "OpenSSL available: #{Iodine::TLS::OPENSSL_AVAILABLE}"
  #   puts "Embedded TLS available: #{Iodine::TLS::EMBEDDED_AVAILABLE}"
  #
  class TLS
    # @return [Boolean] `true` (TLS is always supported - at least embedded is available).
    SUPPORTED = true

    # @return [Boolean] `true` if OpenSSL backend is compiled in and available.
    OPENSSL_AVAILABLE = true

    # @return [Boolean] `true` (embedded TLS 1.3 is always available).
    EMBEDDED_AVAILABLE = true

    # Returns the current default TLS backend.
    #
    # @return [Symbol] `:openssl` or `:iodine` indicating the current default backend
    def self.default; end

    # Sets the default TLS backend.
    #
    # @param backend [Symbol] `:iodine` (embedded TLS 1.3) or `:openssl`
    # @return [Symbol] the new default backend
    # @raise [TypeError] if backend is not a Symbol
    # @raise [ArgumentError] if backend is not `:iodine` or `:openssl`
    # @raise [RuntimeError] if `:openssl` is requested but not available
    #
    # @example
    #   Iodine::TLS.default = :iodine   # Use embedded TLS 1.3
    #   Iodine::TLS.default = :openssl  # Use OpenSSL
    def self.default=(backend); end

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
    # @param name [String, nil] the server name (for SNI)
    # @param cert [String, nil] path to the public certificate PEM file
    # @param key [String, nil] path to the private key PEM file
    # @param password [String, nil] password for the private key
    # @return [self]
    #
    # @example
    #   tls = Iodine::TLS.new
    #   tls.add_cert name: "example.com"
    #   tls.add_cert cert: "my_cert.pem", key: "my_key.pem"
    #   tls.add_cert cert: "my_cert.pem", key: "my_key.pem", password: ENV['TLS_PASS']
    #
    # @note Since TLS setup is crucial for security, an initialization error will result in Iodine crashing with an error message. This is expected behavior.
    def add_cert(name = nil, cert = nil, key = nil, password = nil); end
  end

  #######################

  # Used internally by Iodine.
  class Base
    # Prints the number of object withheld from the GC (for debugging).
    def self.print_debug; end

    # Sets Iodine's cache limit for frozen strings, limited to 65,535 items.
    #
    # @param new_limit [Integer] the new cache limit
    def self.cache_limit=(new_limit); end

    # Returns Iodine's cache limit for frozen strings.
    #
    # @return [Integer] the current cache limit
    def self.cache_limit; end

    # Parses and manages CLI input.
    module CLI
      # Parses CLI input.
      #
      # If `required` is true and CLI detects an error, program will exit with an appropriate exit message and a list of valid CLI options.
      #
      # @param required [Boolean] whether to exit on error
      def parse(required = false); end

      # Returns a CLI option's value.
      #
      # @param key [String] the option key
      # @return [String, nil] the option value
      def [](key); end

      # Sets a CLI option's value.
      #
      # @param key [String] the option key
      # @param value [String] the option value
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
      #
      # @param key [Object] the key
      # @return [Object, nil] the value
      def [](key); end

      # Sets the @value associated with @key and returns it.
      #
      # @param key [Object] the key
      # @param value [Object] the value
      # @return [Object] the value
      def []=(key, value); end

      # Runs block for each key-value pair. Returns the number of times the block was executed.
      #
      # @yield [key, value] the key-value pairs
      # @return [Integer] the number of iterations
      #
      # @example
      #   m = Iodine::Base::MiniMap.new
      #   m[:go] = "home"
      #   m.each {|key, value| puts "#{key} => #{value}"}
      def each(&block); end

      # Returns the number of key-value pairs stored in the Hash.
      # @return [Integer]
      def count; end

      # Empties the map. Returns self.
      # @return [self]
      def clear; end

      # Returns the maximum number of theoretical items the Hash can contain.
      #
      # @return [Integer]
      #
      # @note Capacity increase will likely happen sooner and this number deals more with pre-allocated memory for the objects than with actual capacity.
      def capa; end

      # Reserves room in the Hash Map for at least the @capacity requested.
      #
      # @param capacity [Integer] the capacity to reserve
      # @see #capa
      def reserve(capacity); end

      # Prints a JSON representation of the map (if possible).
      # @return [String]
      def to_s; end

      # Benchmarks MiniMap v.s Hash performance when called from within the C layer.
      #
      # Numbers printed out are in time units. Lower is better.
      #
      # @param number_of_items [Integer] the number of items to benchmark
      # @return [String]
      def self.cbench(number_of_items); end
    end

    # This is an empty web application that does nothing.
    #
    # Iodine provides default implementations for any missing callback (which is all of them in this example). Thus, this app does nothing aside of refusing WebSocket / SSE connections and returning HTTP error 404.
    #
    # This app is used internally when a static file service is requested without a handler. i.e.:
    #
    # ```sh
    # iodine -www ./public_folder
    # ```
    #
    # Which results in a `WARNING: no App found` but no error.
    #
    module App404
    end

    # Provides access to modern cryptographic primitives.
    #
    # This module contains submodules for:
    # - {AES128GCM} - AEAD symmetric encryption with AES-128 (12-byte nonce)
    # - {AES256GCM} - AEAD symmetric encryption with AES-256 (12-byte nonce)
    # - {ChaCha20Poly1305} - AEAD symmetric encryption (12-byte nonce)
    # - {XChaCha20Poly1305} - AEAD symmetric encryption (24-byte nonce, safe for random)
    # - {Ed25519} - Digital signatures (with key conversion to X25519)
    # - {X25519} - Key exchange and public-key encryption (ECIES with ChaCha20, AES-128, or AES-256)
    # - {HKDF} - Key derivation (RFC 5869)
    # - {X25519MLKEM768} - Post-quantum hybrid key encapsulation (X25519 + ML-KEM-768)
    #
    # @note This module is under `Iodine::Base` as the API may change between versions.
    module Crypto
      # ChaCha20-Poly1305 AEAD encryption with 12-byte nonce.
      #
      # @note The 12-byte nonce requires careful management to avoid reuse.
      #   Consider using {XChaCha20Poly1305} if you need random nonces.
      module ChaCha20Poly1305
        # Encrypts data using ChaCha20-Poly1305 AEAD.
        #
        # @param data [String] Plaintext to encrypt
        # @param key [String] 32-byte encryption key
        # @param nonce [String] 12-byte nonce (must be unique per key)
        # @param ad [String, nil] Optional additional authenticated data
        # @return [Array<String, String>] [ciphertext, mac] where mac is 16 bytes
        # @raise [ArgumentError] if key is not 32 bytes or nonce is not 12 bytes
        #
        # @example
        #   key = Iodine::Utils.random(32)
        #   nonce = Iodine::Utils.random(12)
        #   ciphertext, mac = Iodine::Base::Crypto::ChaCha20Poly1305.encrypt("secret", key: key, nonce: nonce)
        def self.encrypt(data, key:, nonce:, ad: nil); end

        # Decrypts data using ChaCha20-Poly1305 AEAD.
        #
        # @param ciphertext [String] Ciphertext to decrypt
        # @param mac [String] 16-byte authentication tag
        # @param key [String] 32-byte encryption key
        # @param nonce [String] 12-byte nonce
        # @param ad [String, nil] Optional additional authenticated data
        # @return [String] Decrypted plaintext
        # @raise [ArgumentError] if key, nonce, or mac have incorrect sizes
        # @raise [RuntimeError] if authentication fails
        #
        # @example
        #   plaintext = Iodine::Base::Crypto::ChaCha20Poly1305.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
        def self.decrypt(ciphertext, mac:, key:, nonce:, ad: nil); end
      end

      # AES-128-GCM AEAD encryption with 12-byte nonce.
      #
      # AES-128-GCM provides authenticated encryption using the AES block cipher
      # with 128-bit keys in Galois/Counter Mode. It is widely supported and
      # offers excellent performance on hardware with AES-NI instructions.
      #
      # @note The 12-byte nonce requires careful management to avoid reuse.
      #   Never use the same nonce twice with the same key.
      module AES128GCM
        # Encrypts data using AES-128-GCM AEAD.
        #
        # @param data [String] Plaintext to encrypt
        # @param key [String] 16-byte encryption key
        # @param nonce [String] 12-byte nonce (must be unique per key)
        # @param ad [String, nil] Optional additional authenticated data
        # @return [Array<String, String>] [ciphertext, mac] where mac is 16 bytes
        # @raise [ArgumentError] if key is not 16 bytes or nonce is not 12 bytes
        #
        # @example
        #   key = Iodine::Utils.random(16)
        #   nonce = Iodine::Utils.random(12)
        #   ciphertext, mac = Iodine::Base::Crypto::AES128GCM.encrypt("secret", key: key, nonce: nonce)
        def self.encrypt(data, key:, nonce:, ad: nil); end

        # Decrypts data using AES-128-GCM AEAD.
        #
        # @param ciphertext [String] Ciphertext to decrypt
        # @param mac [String] 16-byte authentication tag
        # @param key [String] 16-byte encryption key
        # @param nonce [String] 12-byte nonce
        # @param ad [String, nil] Optional additional authenticated data
        # @return [String] Decrypted plaintext
        # @raise [ArgumentError] if key, nonce, or mac have incorrect sizes
        # @raise [RuntimeError] if authentication fails
        #
        # @example
        #   plaintext = Iodine::Base::Crypto::AES128GCM.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
        def self.decrypt(ciphertext, mac:, key:, nonce:, ad: nil); end
      end

      # AES-256-GCM AEAD encryption with 12-byte nonce.
      #
      # AES-256-GCM provides authenticated encryption using the AES block cipher
      # with 256-bit keys in Galois/Counter Mode. It offers a higher security margin
      # than AES-128-GCM and is recommended for long-term security requirements.
      #
      # @note The 12-byte nonce requires careful management to avoid reuse.
      #   Never use the same nonce twice with the same key.
      module AES256GCM
        # Encrypts data using AES-256-GCM AEAD.
        #
        # @param data [String] Plaintext to encrypt
        # @param key [String] 32-byte encryption key
        # @param nonce [String] 12-byte nonce (must be unique per key)
        # @param ad [String, nil] Optional additional authenticated data
        # @return [Array<String, String>] [ciphertext, mac] where mac is 16 bytes
        # @raise [ArgumentError] if key is not 32 bytes or nonce is not 12 bytes
        #
        # @example
        #   key = Iodine::Utils.random(32)
        #   nonce = Iodine::Utils.random(12)
        #   ciphertext, mac = Iodine::Base::Crypto::AES256GCM.encrypt("secret", key: key, nonce: nonce)
        def self.encrypt(data, key:, nonce:, ad: nil); end

        # Decrypts data using AES-256-GCM AEAD.
        #
        # @param ciphertext [String] Ciphertext to decrypt
        # @param mac [String] 16-byte authentication tag
        # @param key [String] 32-byte encryption key
        # @param nonce [String] 12-byte nonce
        # @param ad [String, nil] Optional additional authenticated data
        # @return [String] Decrypted plaintext
        # @raise [ArgumentError] if key, nonce, or mac have incorrect sizes
        # @raise [RuntimeError] if authentication fails
        #
        # @example
        #   plaintext = Iodine::Base::Crypto::AES256GCM.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
        def self.decrypt(ciphertext, mac:, key:, nonce:, ad: nil); end
      end

      # XChaCha20-Poly1305 AEAD encryption with 24-byte nonce.
      #
      # The extended 24-byte nonce makes it safe to use randomly generated nonces
      # without risk of collision, unlike the 12-byte nonce in ChaCha20-Poly1305.
      module XChaCha20Poly1305
        # Encrypts data using XChaCha20-Poly1305 AEAD.
        #
        # @param data [String] Plaintext to encrypt
        # @param key [String] 32-byte encryption key
        # @param nonce [String] 24-byte nonce (safe for random generation)
        # @param ad [String, nil] Optional additional authenticated data
        # @return [Array<String, String>] [ciphertext, mac] where mac is 16 bytes
        # @raise [ArgumentError] if key is not 32 bytes or nonce is not 24 bytes
        #
        # @example
        #   key = Iodine::Utils.random(32)
        #   nonce = Iodine::Utils.random(24)  # Safe to use random nonce!
        #   ciphertext, mac = Iodine::Base::Crypto::XChaCha20Poly1305.encrypt("secret", key: key, nonce: nonce)
        def self.encrypt(data, key:, nonce:, ad: nil); end

        # Decrypts data using XChaCha20-Poly1305 AEAD.
        #
        # @param ciphertext [String] Ciphertext to decrypt
        # @param mac [String] 16-byte authentication tag
        # @param key [String] 32-byte encryption key
        # @param nonce [String] 24-byte nonce
        # @param ad [String, nil] Optional additional authenticated data
        # @return [String] Decrypted plaintext
        # @raise [ArgumentError] if key, nonce, or mac have incorrect sizes
        # @raise [RuntimeError] if authentication fails
        def self.decrypt(ciphertext, mac:, key:, nonce:, ad: nil); end
      end

      # Ed25519 digital signatures (128-bit security level).
      #
      # Ed25519 provides fast, secure digital signatures suitable for
      # authentication, message signing, and other cryptographic protocols.
      module Ed25519
        # Generates a new Ed25519 key pair.
        #
        # @return [Array<String, String>] [secret_key, public_key] both 32 bytes
        #
        # @example
        #   secret_key, public_key = Iodine::Base::Crypto::Ed25519.keypair
        def self.keypair; end

        # Derives the public key from an Ed25519 secret key.
        #
        # @param secret_key [String] 32-byte secret key
        # @return [String] 32-byte public key
        # @raise [ArgumentError] if secret_key is not 32 bytes
        def self.public_key(secret_key:); end

        # Signs a message using Ed25519.
        #
        # @param message [String] Message to sign
        # @param secret_key [String] 32-byte secret key
        # @param public_key [String] 32-byte public key
        # @return [String] 64-byte signature
        # @raise [ArgumentError] if keys have incorrect sizes
        #
        # @example
        #   signature = Iodine::Base::Crypto::Ed25519.sign("message", secret_key: sk, public_key: pk)
        def self.sign(message, secret_key:, public_key:); end

        # Verifies an Ed25519 signature.
        #
        # @param signature [String] 64-byte signature
        # @param message [String] Original message
        # @param public_key [String] 32-byte public key
        # @return [Boolean] `true` if valid, `false` otherwise
        # @raise [ArgumentError] if signature or public_key have incorrect sizes
        #
        # @example
        #   valid = Iodine::Base::Crypto::Ed25519.verify(signature, "message", public_key: pk)
        def self.verify(signature, message, public_key:); end

        # Converts an Ed25519 secret key to an X25519 secret key.
        #
        # This allows using the same keypair for both signing (Ed25519) and
        # encryption (X25519), reducing key management complexity.
        #
        # @param ed_secret_key [String] 32-byte Ed25519 secret key
        # @return [String] 32-byte X25519 secret key
        # @raise [ArgumentError] if ed_secret_key is not 32 bytes
        #
        # @example
        #   # Generate Ed25519 keypair for signing
        #   ed_sk, ed_pk = Iodine::Base::Crypto::Ed25519.keypair
        #
        #   # Convert to X25519 for encryption
        #   x_sk = Iodine::Base::Crypto::Ed25519.to_x25519_secret(ed_secret_key: ed_sk)
        #   x_pk = Iodine::Base::Crypto::Ed25519.to_x25519_public(ed_public_key: ed_pk)
        #
        #   # Now use x_sk and x_pk for X25519 encryption
        def self.to_x25519_secret(ed_secret_key:); end

        # Converts an Ed25519 public key to an X25519 public key.
        #
        # This allows encrypting to someone who has only shared their Ed25519
        # signing public key, without requiring a separate X25519 public key.
        #
        # @param ed_public_key [String] 32-byte Ed25519 public key
        # @return [String] 32-byte X25519 public key
        # @raise [ArgumentError] if ed_public_key is not 32 bytes
        #
        # @example
        #   # Alice has Bob's Ed25519 public key (for signature verification)
        #   # She can derive his X25519 public key to encrypt a message
        #   bob_x_pk = Iodine::Base::Crypto::Ed25519.to_x25519_public(ed_public_key: bob_ed_pk)
        #   ciphertext = Iodine::Base::Crypto::X25519.encrypt("secret", recipient_pk: bob_x_pk)
        def self.to_x25519_public(ed_public_key:); end
      end

      # X25519 key exchange (ECDH) and public-key encryption (ECIES).
      #
      # X25519 provides secure key agreement and can be combined with
      # symmetric encryption for public-key encryption without prior key exchange.
      module X25519
        # Generates a new X25519 key pair.
        #
        # @return [Array<String, String>] [secret_key, public_key] both 32 bytes
        #
        # @example
        #   secret_key, public_key = Iodine::Base::Crypto::X25519.keypair
        def self.keypair; end

        # Derives the public key from an X25519 secret key.
        #
        # @param secret_key [String] 32-byte secret key
        # @return [String] 32-byte public key
        # @raise [ArgumentError] if secret_key is not 32 bytes
        def self.public_key(secret_key:); end

        # Computes a shared secret using X25519 (ECDH).
        #
        # Both parties compute the same shared secret:
        #   shared = X25519(my_secret, their_public)
        #
        # @param secret_key [String] 32-byte own secret key
        # @param their_public [String] 32-byte other party's public key
        # @return [String] 32-byte shared secret
        # @raise [ArgumentError] if keys have incorrect sizes
        # @raise [RuntimeError] if key exchange fails (e.g., low-order point)
        #
        # @example
        #   # Alice and Bob each generate keypairs
        #   alice_sk, alice_pk = Iodine::Base::Crypto::X25519.keypair
        #   bob_sk, bob_pk = Iodine::Base::Crypto::X25519.keypair
        #
        #   # They exchange public keys and compute the same shared secret
        #   alice_shared = Iodine::Base::Crypto::X25519.shared_secret(secret_key: alice_sk, their_public: bob_pk)
        #   bob_shared = Iodine::Base::Crypto::X25519.shared_secret(secret_key: bob_sk, their_public: alice_pk)
        #   # alice_shared == bob_shared
        def self.shared_secret(secret_key:, their_public:); end

        # Encrypts a message using X25519 public-key encryption (ECIES).
        #
        # Uses ephemeral key agreement + ChaCha20-Poly1305 for authenticated
        # encryption. Only the recipient with the matching secret key can decrypt.
        #
        # @param message [String] Plaintext to encrypt
        # @param recipient_pk [String] 32-byte recipient's public key
        # @return [String] Ciphertext (message.length + 48 bytes overhead)
        # @raise [ArgumentError] if recipient_pk is not 32 bytes
        # @raise [RuntimeError] if encryption fails
        #
        # @example
        #   # Bob encrypts a message for Alice
        #   ciphertext = Iodine::Base::Crypto::X25519.encrypt("secret message", recipient_pk: alice_pk)
        def self.encrypt(message, recipient_pk:); end

        # Decrypts a message using X25519 public-key encryption (ECIES).
        #
        # @param ciphertext [String] Ciphertext from X25519.encrypt
        # @param secret_key [String] 32-byte recipient's secret key
        # @return [String] Decrypted plaintext
        # @raise [ArgumentError] if secret_key is not 32 bytes or ciphertext is too short
        # @raise [RuntimeError] if decryption fails (authentication error)
        #
        # @example
        #   # Alice decrypts the message
        #   plaintext = Iodine::Base::Crypto::X25519.decrypt(ciphertext, secret_key: alice_sk)
        def self.decrypt(ciphertext, secret_key:); end

        # Encrypts a message using X25519 ECIES with AES-128-GCM.
        #
        # Uses ephemeral key agreement + AES-128-GCM for authenticated encryption.
        # Only the recipient with the matching secret key can decrypt.
        # The shared secret is derived using HKDF before use as the AES key.
        #
        # @param message [String] Plaintext to encrypt
        # @param recipient_pk [String] 32-byte recipient's public key
        # @return [String] Ciphertext (message.length + 48 bytes overhead)
        # @raise [ArgumentError] if recipient_pk is not 32 bytes
        # @raise [RuntimeError] if encryption fails
        #
        # @example
        #   # Bob encrypts a message for Alice using AES-128-GCM
        #   ciphertext = Iodine::Base::Crypto::X25519.encrypt_aes128("secret message", recipient_pk: alice_pk)
        def self.encrypt_aes128(message, recipient_pk:); end

        # Decrypts a message using X25519 ECIES with AES-128-GCM.
        #
        # @param ciphertext [String] Ciphertext from X25519.encrypt_aes128
        # @param secret_key [String] 32-byte recipient's secret key
        # @return [String] Decrypted plaintext
        # @raise [ArgumentError] if secret_key is not 32 bytes or ciphertext is too short
        # @raise [RuntimeError] if decryption fails (authentication error)
        #
        # @example
        #   # Alice decrypts the message
        #   plaintext = Iodine::Base::Crypto::X25519.decrypt_aes128(ciphertext, secret_key: alice_sk)
        def self.decrypt_aes128(ciphertext, secret_key:); end

        # Encrypts a message using X25519 ECIES with AES-256-GCM.
        #
        # Uses ephemeral key agreement + AES-256-GCM for authenticated encryption.
        # Only the recipient with the matching secret key can decrypt.
        # The shared secret is derived using HKDF before use as the AES key.
        #
        # @param message [String] Plaintext to encrypt
        # @param recipient_pk [String] 32-byte recipient's public key
        # @return [String] Ciphertext (message.length + 48 bytes overhead)
        # @raise [ArgumentError] if recipient_pk is not 32 bytes
        # @raise [RuntimeError] if encryption fails
        #
        # @example
        #   # Bob encrypts a message for Alice using AES-256-GCM
        #   ciphertext = Iodine::Base::Crypto::X25519.encrypt_aes256("secret message", recipient_pk: alice_pk)
        def self.encrypt_aes256(message, recipient_pk:); end

        # Decrypts a message using X25519 ECIES with AES-256-GCM.
        #
        # @param ciphertext [String] Ciphertext from X25519.encrypt_aes256
        # @param secret_key [String] 32-byte recipient's secret key
        # @return [String] Decrypted plaintext
        # @raise [ArgumentError] if secret_key is not 32 bytes or ciphertext is too short
        # @raise [RuntimeError] if decryption fails (authentication error)
        #
        # @example
        #   # Alice decrypts the message
        #   plaintext = Iodine::Base::Crypto::X25519.decrypt_aes256(ciphertext, secret_key: alice_sk)
        def self.decrypt_aes256(ciphertext, secret_key:); end
      end

      # HKDF key derivation (RFC 5869).
      #
      # HKDF is a simple and secure key derivation function based on HMAC.
      # It can be used to derive multiple keys from a single source of keying material.
      module HKDF
        # Derives keying material using HKDF (RFC 5869).
        #
        # @param ikm [String] Input keying material
        # @param salt [String, nil] Optional salt (random value)
        # @param info [String, nil] Optional context/application info
        # @param length [Integer] Desired output length (default: 32)
        # @param sha384 [Boolean] Use SHA-384 instead of SHA-256 (default: false)
        # @return [String] Derived key material
        # @raise [ArgumentError] if length is out of range
        #
        # @example
        #   # Derive a 32-byte key from a password
        #   key = Iodine::Base::Crypto::HKDF.derive(ikm: password, salt: salt, info: "encryption key", length: 32)
        #
        # @example
        #   # Derive multiple keys from shared secret
        #   shared = X25519.shared_secret(...)
        #   enc_key = HKDF.derive(ikm: shared, info: "encryption", length: 32)
        #   mac_key = HKDF.derive(ikm: shared, info: "authentication", length: 32)
        def self.derive(ikm:, salt: nil, info: nil, length: 32, sha384: false); end
      end

      # X25519+ML-KEM-768 Post-Quantum Hybrid Key Encapsulation Mechanism.
      #
      # X25519MLKEM768 combines classical X25519 elliptic curve Diffie-Hellman with
      # ML-KEM-768 (formerly known as Kyber), a post-quantum lattice-based KEM.
      # This hybrid approach provides security against both classical and quantum
      # computer attacks.
      #
      # The hybrid construction ensures that even if one algorithm is broken,
      # the other still provides security. This is the recommended approach for
      # transitioning to post-quantum cryptography.
      #
      # Key sizes:
      # - Secret key: 2432 bytes (ML-KEM-768 sk + X25519 sk)
      # - Public key: 1216 bytes (ML-KEM-768 pk + X25519 pk)
      # - Ciphertext: 1120 bytes (ML-KEM-768 ct + X25519 ephemeral pk)
      # - Shared secret: 64 bytes (ML-KEM-768 ss || X25519 ss)
      #
      # @note This is a Key Encapsulation Mechanism (KEM), not direct encryption.
      #   Use the shared secret with a symmetric cipher (e.g., AES-256-GCM or
      #   ChaCha20-Poly1305) to encrypt actual data.
      #
      # @example Complete key exchange workflow
      #   # Recipient generates a keypair
      #   secret_key, public_key = Iodine::Base::Crypto::X25519MLKEM768.keypair
      #
      #   # Sender encapsulates a shared secret using recipient's public key
      #   ciphertext, sender_shared = Iodine::Base::Crypto::X25519MLKEM768.encapsulate(public_key: public_key)
      #
      #   # Recipient decapsulates to obtain the same shared secret
      #   recipient_shared = Iodine::Base::Crypto::X25519MLKEM768.decapsulate(
      #     ciphertext: ciphertext,
      #     secret_key: secret_key
      #   )
      #
      #   # sender_shared == recipient_shared (64 bytes)
      #   # Now both parties can use the shared secret for symmetric encryption
      module X25519MLKEM768
        # Generates a new X25519+ML-KEM-768 hybrid key encapsulation keypair.
        #
        # @return [Array<String, String>] [secret_key, public_key]
        #   - secret_key: 2432-byte binary string
        #   - public_key: 1216-byte binary string
        # @raise [RuntimeError] if key generation fails
        #
        # @note Post-quantum secure. The keypair combines classical X25519 with
        #   ML-KEM-768 (Kyber) for protection against both classical and quantum attacks.
        #
        # @example
        #   secret_key, public_key = Iodine::Base::Crypto::X25519MLKEM768.keypair
        #   # secret_key.bytesize => 2432
        #   # public_key.bytesize => 1216
        def self.keypair; end

        # Encapsulates a shared secret for the given public key.
        #
        # The sender uses this method to generate a shared secret and ciphertext.
        # The ciphertext is sent to the recipient, who can decapsulate it using
        # their secret key to obtain the same shared secret.
        #
        # @param public_key [String] 1216-byte recipient's public key
        # @return [Array<String, String>] [ciphertext, shared_secret]
        #   - ciphertext: 1120-byte binary string to send to recipient
        #   - shared_secret: 64-byte binary string for symmetric encryption
        # @raise [ArgumentError] if public_key is not 1216 bytes
        # @raise [RuntimeError] if encapsulation fails
        #
        # @note Send the ciphertext to the recipient who can decapsulate it to
        #   obtain the same shared secret. The shared secret should be used with
        #   a symmetric cipher for actual data encryption.
        #
        # @example
        #   # Sender encapsulates a shared secret
        #   ciphertext, shared_secret = Iodine::Base::Crypto::X25519MLKEM768.encapsulate(
        #     public_key: recipient_public_key
        #   )
        #   # Send ciphertext to recipient
        #   # Use shared_secret with AES-256-GCM or ChaCha20-Poly1305
        def self.encapsulate(public_key:); end

        # Decapsulates a shared secret using your secret key.
        #
        # The recipient uses this method to recover the shared secret from the
        # ciphertext sent by the sender.
        #
        # @param ciphertext [String] 1120-byte ciphertext from encapsulate
        # @param secret_key [String] 2432-byte secret key from keypair
        # @return [String] 64-byte shared secret (same as encapsulate returned)
        # @raise [ArgumentError] if ciphertext is not 1120 bytes or secret_key is not 2432 bytes
        # @raise [RuntimeError] if decapsulation fails (invalid key or ciphertext)
        #
        # @example
        #   # Recipient decapsulates the shared secret
        #   shared_secret = Iodine::Base::Crypto::X25519MLKEM768.decapsulate(
        #     ciphertext: received_ciphertext,
        #     secret_key: my_secret_key
        #   )
        #   # shared_secret matches what the sender obtained from encapsulate
        def self.decapsulate(ciphertext:, secret_key:); end
      end
    end
  end

  # class Error < StandardError; end
end

# end # defined?(Iodine)
