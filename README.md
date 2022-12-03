# iodine - Why Settle for a fast HTTP / WebSocket Server with native Pub/Sub?

[![Gem](https://img.shields.io/gem/dt/iodine.svg)](https://rubygems.org/gems/iodine)
[![Build Status](https://github.com/boazsegev/iodine/actions/workflows/ruby.yml/badge.svg)](https://github.com/boazsegev/iodine/actions/workflows/ruby.yml)
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

[![Logo](https://github.com/boazsegev/iodine/raw/master/logo.png)](https://github.com/boazsegev/iodine)

Iodine is a fast concurrent web application server for real-time Ruby applications, with native support for WebSockets and Pub/Sub services - but it's also so much more.

Iodine is a Ruby wrapper for many of the [facil.io](https://facil.io) C framework, leveraging the speed of C for many common web application tasks. In addition, iodine abstracts away all network concerns, so you never need to worry about the transport layer, free to concentrate on your application logic.

Iodine includes native support for:

* HTTP, WebSockets and EventSource (SSE) Services (server);
* WebSocket connections (server / client);
* Pub/Sub (with optional Redis Pub/Sub scaling);
* Fast(!) builtin Mustache template engine.
* Static file service (with automatic `gzip` support for pre-compressed assets);
* Optimized Logging to `stderr`.
* Asynchronous event scheduling and timers;
* HTTP/1.1 keep-alive and pipelining;
* Heap Fragmentation Protection.
* TLS 1.2 and above (Requires OpenSSL >= 1.1.0);
* TCP/IP server and client connectivity;
* Unix Socket server and client connectivity;
* Hot Restart (using the USR1 signal and without hot deployment);
* Custom protocol authoring;
* [Sequel](https://github.com/jeremyevans/sequel) and ActiveRecord forking protection.
* and more!

Since iodine wraps much of the [C facil.io framework](https://github.com/boazsegev/facil.io) to Ruby:

* Iodine can handle **thousands of concurrent connections** (tested with more then 20K connections on Linux)!

* Iodine is ideal for **Linux/Unix** based systems (i.e. macOS, Ubuntu, FreeBSD etc'), which are ideal for evented IO (while Windows and Solaris are better at IO *completion* events, which are very different).

Iodine is a C extension for Ruby, developed and optimized for Ruby MRI 2.3 and up... it should support the whole Ruby 2.x and 3.x MRI family, but CI tests start at Ruby 2.3.

**Note**: iodine does **not** support streaming when using Rack. It's recommended to avoid blocking the server when using `body.each` since the `each` loop will block the iodine's thread until it's finished and iodine won't send any data before the loop is done.

## Iodine - a fast & powerful HTTP + WebSockets server with native Pub/Sub

Iodine includes a light and fast HTTP and Websocket server written in C that was written according to the [Rack interface specifications](http://www.rubydoc.info/github/rack/rack/master/file/SPEC) and the [Websocket draft extension](./SPEC-Websocket-Draft.md).

With `Iodine.listen service: :http` it's possible to run multiple HTTP applications (please remember not to set more than a single application on a single TCP/IP port). 

Iodine also supports native process cluster Pub/Sub and a native RedisEngine to easily scale iodine's Pub/Sub horizontally.

### Known Issues and Reporting Issues

See the [GitHub Open Issues](https://github.com/boazsegev/iodine/issues) list for known issues and to report new issues.

### Installing and Running Iodine

Install iodine on any Linux / BSD / macOS system using:

```bash
gem install iodine
```

Using the iodine server is easy, simply add iodine as a gem to your Rails / Sinatra / Rack application's `Gemfile`:

```ruby
gem 'iodine', '~>0.7'
```

Then start your application from the command-line / terminal using iodine:

```bash
bundler exec iodine
```

#### Installing with SSL/TLS

**Note**: iodine has known issues with the TLS/SSL support. TLS/SSL should **NOT** be used in production (see issues #95 and #94).

Make sure to update OpenSSL to the latest version **before installing Ruby** (`rbenv` should do this automatically).

To avoid name resolution conflicts, iodine will bind to the same OpenSSL version Ruby is bound to. To use SSL/TLS this should be OpenSSL >= 1.1.0 or LibreSSL >= 2.7.4.

Verbose installation should provide a confirmation message, such as:

```bash
$ gem install iodine -f -V
...
checking for -lcrypto... yes
checking for -lssl... yes
Detected OpenSSL library, testing for version.
Confirmed OpenSSL to be version 1.1.0 or above (OpenSSL 1.1.0j  20 Nov 2018)...
* Compiling with HAVE_OPENSSL.
...
```

The installation script tests for OpenSSL 1.1.0 and above. However, this testing approach sometimes provides false positives. **If TLS isn't required, install with `NO_SSL=1`**. i.e.:

```bash
NO_SSL=1 bundler exec iodine
```

### Running with Rails

On Rails:

1. Replace the `puma` gem with the `iodine` gem.

1. Remove the `config/puma.rb` file (or comment out the code).

1. Optionally, it's possible to add a `config/initializers/iodine.rb` file. For example:

    ```ruby
    # Iodine setup - use conditional setup to allow command-line arguments to override these:
    if(defined?(Iodine))
      Iodine.threads = ENV.fetch("RAILS_MAX_THREADS", 5).to_i if Iodine.threads.zero?
      Iodine.workers = ENV.fetch("WEB_CONCURRENCY", 2).to_i if Iodine.workers.zero?
      Iodine::DEFAULT_SETTINGS[:port] ||= ENV.fetch("PORT") if ENV.fetch("PORT")
    end
    ```

When using native WebSockets with Rails, middle-ware is probably the best approach. A guide for this approach will, hopefully, get published in the future.

**Note**: command-line instructions (CLI) should be the preferred way for configuring iodine, allowing for code-less configuration updates.

### Optimizing Iodine's Concurrency

To get the most out of iodine, consider the amount of CPU cores available and the concurrency level the application requires.

Iodine will calculate, when possible, a good enough default concurrency model for fast applications. See if this works for your application or customize according to the application's needs.

Command line arguments allow easy access to different options, including concurrency levels. i.e., to set up 16 threads and 4 processes:

```bash
bundler exec iodine -p $PORT -t 16 -w 4
```

The environment variables `THREADS` and `WORKERS` are automatically recognized when iodine is first required, allowing environment specific customization. i.e.:

```bash
export THREADS=16
export WORKERS=-1 # negative values are fractions of CPU cores.
bundler exec iodine -p $PORT
```

Negative values are evaluated as "CPU Cores / abs(Value)". i.e., on an 8 core CPU machine, this will produce 4 worker processes with 2 threads per worker:

```bash
bundler exec iodine -p $PORT -t 2 -w -2
```

### Heap Fragmentation Protection

Iodine includes a fast, network oriented, custom memory allocator, optimizing away some of the work usually placed on the Ruby Garbage Collector (GC).

This approach helps to minimize heap fragmentation for long running processes, by grouping many short-lived objects into a common memory space.

It is still recommended to consider [jemalloc](http://jemalloc.net) or other allocators that also help mitigate heap fragmentation issues.

### Static file serving support

Iodine supports an internal static file service that bypasses the Ruby layer  and serves static files directly from "C-land".

This means that iodine won't lock Ruby's GVL when sending static files. The files will be sent directly, allowing for true native concurrency.

Since the Ruby layer is unaware of these requests, logging can be performed by turning iodine's logger on.

To use native static file service, setup the public folder's address **before** starting the server.

This can be done when starting the server from the command line:

```bash
bundler exec iodine -p $PORT -t 16 -w 4 -www /my/public/folder
```

Or using a simple Ruby script. i.e. (a `my_server.rb` example):

```ruby
require 'iodine'
# static file service
Iodine.listen, service: :http, public: '/my/public/folder'
# for static file service, we only need a single thread and a single worker.
Iodine.threads = 1
Iodine.start
```

To enable logging from the command line, use the `-v` (verbose) option:

```bash
bundler exec iodine -p $PORT -t 16 -w 4 -www /my/public/folder -v
```

#### X-Sendfile

When a public folder is assigned (the static file server is active), iodine automatically adds support for the `X-Sendfile` header in any Ruby application response.

This allows Ruby to send very large files using a very small memory footprint and usually leverages the `sendfile` system call.

i.e. (example `config.ru` for iodine):

```ruby
app = proc do |env|
  request = Rack::Request.new(env)
  if request.path_info == '/source'.freeze
    [200, { 'X-Sendfile' => File.expand_path(__FILE__), 'Content-Type' => 'text/plain'}, []]
  elsif request.path_info == '/file'.freeze
    [200, { 'X-Header' => 'This was a Rack::Sendfile response sent as text.' }, File.open(__FILE__)]
  else
    [200, { 'Content-Type' => 'text/html',
            'Content-Length' => request.path_info.length.to_s },
     [request.path_info]]
 end
end
# # optional:
# use Rack::Sendfile
run app
```

Benchmark [localhost:3000/source](http://localhost:3000/source) to experience the `X-Sendfile` extension at work.

#### Pre-Compressed assets / files

Rails does this automatically when compiling assets, which is: `gzip` your static files.

Iodine will automatically recognize and send the `gz` version if the client (browser) supports the `gzip` transfer-encoding.

For example, to offer a compressed version of `style.css`, run (in the terminal):

```bash
$  gzip -k -9 style.css
```

This results in both files, `style.css` (the original) and `style.css.gz` (the compressed).

When a browser that supports compressed encoding (which is most browsers) requests the file, iodine will recognize that a pre-compressed option exists and will prefer the `gzip` compressed version.

It's as easy as that. No extra code required.

### Special HTTP `Upgrade` and SSE support

Iodine's HTTP server implements the [WebSocket/SSE Rack Specification Draft](SPEC-Websocket-Draft.md), supporting native WebSocket/SSE connections using Rack's `env` Hash.

This promotes separation of concerns, where iodine handles all the Network related logic and the application can focus on the API and data it provides.

Upgrading an HTTP connection can be performed either using iodine's native WebSocket / EventSource (SSE) support with `env['rack.upgrade?']` or by implementing your own protocol directly over the TCP/IP layer - be it a WebSocket flavor or something completely different - using `env['upgrade.tcp']`.

#### EventSource / SSE

Iodine treats EventSource / SSE connections as if they were a half-duplex WebSocket connection, using the exact same API and callbacks as WebSockets.

When an EventSource / SSE request is received, iodine will set the Rack Hash's upgrade property to `:sse`, so that: `env['rack.upgrade?'] == :sse`.

The rest is detailed in the WebSocket support section.

#### WebSockets

When a WebSocket connection request is received, iodine will set the Rack Hash's upgrade property to `:websocket`, so that: `env['rack.upgrade?'] == :websocket`

To "upgrade" the HTTP request to the WebSockets protocol (or SSE), simply provide iodine with a WebSocket Callback Object instance or class: `env['rack.upgrade'] = MyWebsocketClass` or `env['rack.upgrade'] = MyWebsocketClass.new(args)`

Iodine will adopt the object, providing it with network functionality (methods such as `write`, `defer` and `close` will become available) and invoke it's callbacks on network events.

Here is a simple chat-room example we can run in the terminal (`irb`) or easily paste into a `config.ru` file:

```ruby
require 'iodine'
module WebsocketChat
  def on_open client
    # Pub/Sub directly to the client (or use a block to process the messages)
    client.subscribe :chat
    # Writing directly to the socket
    client.write "You're now in the chatroom."
  end
  def on_message client, data
    # Strings and symbol channel names are equivalent.
    client.publish "chat", data
  end
  extend self
end
APP = Proc.new do |env|
  if env['rack.upgrade?'.freeze] == :websocket 
    env['rack.upgrade'.freeze] = WebsocketChat 
    [0,{}, []] # It's possible to set cookies for the response.
  elsif env['rack.upgrade?'.freeze] == :sse
    puts "SSE connections can only receive data from the server, the can't write." 
    env['rack.upgrade'.freeze] = WebsocketChat
    [0,{}, []] # It's possible to set cookies for the response.
  else
    [200, {"Content-Length" => "12", "Content-Type" => "text/plain"}, ["Welcome Home"] ]
  end
end
# Pus/Sub can be server oriented as well as connection bound
Iodine.subscribe(:chat) {|ch, msg| puts msg if Iodine.master? }
# By default, Pub/Sub performs in process cluster mode.
Iodine.workers = 4
# # in irb:
Iodine.listen service: :http, public: "www/public", handler: APP
Iodine.start
# # or in config.ru
run APP
```

### Native Pub/Sub with *optional* Redis scaling

Iodine's core, `facil.io` offers a native Pub/Sub implementation that can be scaled across machine boundaries using Redis.

The default implementation covers the whole process cluster, so a single cluster doesn't need Redis

Once a single iodine process cluster isn't enough, horizontal scaling for the Pub/Sub layer is as simple as connecting iodine to Redis using the `-r <url>` from the command line. i.e.:

```bash
$ iodine -w -1 -t 8 -r redis://localhost
```

It's also possible to initialize the iodine<=>Redis link using Ruby, directly from the application's code:

```ruby
# initialize the Redis engine for each iodine process.
if ENV["REDIS_URL"]
  Iodine::PubSub.default = Iodine::PubSub::Redis.new(ENV["REDIS_URL"])
else
  puts "* No Redis, it's okay, pub/sub will still run on the whole process cluster."
end
# ... the rest of the application remains unchanged.
```

Iodine's Redis client can also be used for asynchronous Redis command execution. i.e.:

```ruby
if(Iodine::PubSub.default.is_a? Iodine::PubSub::Redis)
  # Ask Redis about all it's client connections and print out the reply.
  Iodine::PubSub.default.cmd("CLIENT LIST") { |reply| puts reply }
end
```

**Pub/Sub Details and Limitations:**

* Iodine's Redis client does *not* support multiple databases. This is both because [database scoping is ignored by Redis during pub/sub](https://redis.io/topics/pubsub#database-amp-scoping) and because [Redis Cluster doesn't support multiple databases](https://redis.io/topics/cluster-spec). This indicated that multiple database support just isn't worth the extra effort and performance hit.

* The iodine Redis client will use two Redis connections for the whole process cluster (a single publishing connection and a single subscription connection), minimizing the Redis load and network bandwidth.

* Connections will be automatically re-established if timeouts or errors occur.

### Hot Restart

Iodine will "hot-restart" the application by shutting down and re-spawning the worker processes.

This will clear away any memory fragmentation concerns and other issues that might plague a long running worker process or ruby application.

To hot-restart iodine, send the `SIGUSR1` signal to the root process.

The following code will hot-restart iodine every 4 hours when iodine is running in cluster mode:

```ruby
Iodine.run_every(4 * 60 * 60 * 1000) do
  Process.kill("SIGUSR1", Process.pid) unless Iodine.worker?
end
```

Since the master / root process doesn't handle any requests (it only handles pub/sub and house-keeping), it's memory map and process data shouldn't be as affected and the new worker processes should be healthier and more performant.

**Note**: This will **not** re-load the application (any changes to the Ruby code require an actual restart).

### Optimized HTTP logging

By default, iodine is pretty quiet. Some messages are logged to `stderr`, but not many.

However, HTTP requests can be logged using iodine's optimized logger to `stderr`. Iodine will optimize the log output by caching the output time string which updates every second rather than every request.

This can be performed by setting the `-v` flag during startup, i.e.:

```bash
bundler exec iodine -p $PORT -t 16 -w 4 -v -www /my/public/folder
```

The log output can be redirected to a file:

```bash
bundler exec iodine -p $PORT -v  2>my_log.log
```

The log output can also be redirected to a `stdout`:

```bash
bundler exec iodine -p $PORT -v  2>&1
```

### Built-in support for Sequel and ActiveRecord

It's a well known fact that [Database connections require special attention when using `fork`-ing servers (multi-process servers)](https://devcenter.heroku.com/articles/concurrency-and-database-connections#multi-process-servers) such as Puma, Passenger (Pro) and iodine.

However, it's also true that [these issues go unnoticed by many developers](https://stackoverflow.com/a/45570999/4025095), since application developers are (rightfully) focused on the application rather than the infrastructure.

With iodine, there's no need to worry.

Iodine provides built-in `fork` handling for both ActiveRecord and [Sequel](https://github.com/jeremyevans/sequel), in order to protect against these possible errors.

### Client Support

Iodine supports raw (TCP/IP and Unix Sockets) client connections as well as WebSocket connections.

This can be utilized for communicating across micro services or taking advantage of persistent connection APIs such as ActionCable APIs, socket.io APIs etc'.

Here is an example WebSocket client that will connect to the [WebSocket.org echo test service](https://www.websocket.org/echo.html) and send a number of pre-programmed messages.

```ruby
require 'iodine'

# The client class
class EchoClient

  def on_open(connection)
    @messages = [ "Hello World!",
      "I'm alive and sending messages",
      "I also receive messages",
      "now that we all know this...",
      "I can stop.",
      "Goodbye." ]
    send_one_message(connection)
  end

  def on_message(connection, message)
    puts "Received: #{message}"
    send_one_message(connection)
  end

  def on_close(connection)
    # in this example, we stop iodine once the client is closed
    puts "* Client closed."
    Iodine.stop
  end

  # We use this method to pop messages from the queue and send them
  #
  # When the queue is empty, we disconnect the client.
  def send_one_message(connection)
    msg = @messages.shift
    if(msg)
      connection.write msg
    else
      connection.close
    end
  end
end

Iodine.threads = 1
Iodine.connect url: "wss://echo.websocket.org", handler: EchoClient.new, ping: 40
Iodine.start
```

### TLS >= 1.2 support

>  Requires OpenSSL >= `1.1.0`. On Heroku, requires `heroku-18`.

Iodine supports secure connections fore TLS version 1.2 **and up** (depending on the OpenSSL version).

A self signed certificate is available using the `-tls` flag from the command-line.

PEM encoded certificates (which is probably the most common format) can be loaded from the command-line (`-tls-cert` and `-tls-key`) or dynamically (using `Iodine::TLS`).

The TLS API is simplified but powerful, supporting the ALPN extension and peer verification (which client connections really should leverage).

When enabling peer verification for server connections (using `Iodine::TLS#trust`), clients will be required to submit a trusted certificate in order to connect to the server.

### TCP/IP (raw) sockets

Upgrading to a custom protocol (i.e., in order to implement your own WebSocket protocol with special extensions) is available when neither WebSockets nor SSE connection upgrades were requested. In the following (terminal) example, we'll use an echo server without direct socket echo:

```ruby
require 'iodine'
class MyProtocol
  def on_message client, data
    # regular socket echo - NOT websockets
    client.write data
  end
end
APP = Proc.new do |env|
  if env["HTTP_UPGRADE".freeze] =~ /echo/i.freeze
    env['upgrade.tcp'.freeze] = MyProtocol.new
    # an HTTP response will be sent before changing protocols.
    [101, { "Upgrade" => "echo" }, []]
  else
    [200, {"Content-Length" => "12", "Content-Type" => "text/plain"}, ["Welcome Home"] ]
  end
end
# # in irb:
Iodine.listen service: :http, public: "www/public", handler: APP
Iodine.threads = 1
Iodine.start
# # or in config.ru
run APP
```

### How does it compare to other servers?

In my tests, pitching Iodine against Puma, Iodine was anywhere between x1.5 and more than x10 faster than Puma (depending on use-case and settings).

Such a big difference is suspect and I recommend that you test it yourself - even better if you test performance using your own application and a number of possible different settings (how many threads per CPU core? how many worker processes? middleware vs. server request logging, etc').

I recommend benchmarking the performance for yourself using `wrk` or `ab`:

```bash
$ wrk -c200 -d4 -t2 http://localhost:3000/
# or
$ ab -n 100000 -c 200 -k http://127.0.0.1:3000/
```

The best application to use for benchmarking is your actual application. Or, you could create a simple `config.ru` file with a __hello world__ app:

```ruby
App = Proc.new do |env|
   [200,
     {   "Content-Type" => "text/html".freeze,
         "Content-Length" => "16".freeze },
     ['Hello from Rack!'.freeze]  ]
end

run App
```

Then start comparing servers. Here are the settings I used to compare iodine and Puma (4 processes, 4 threads):

```bash
$ RACK_ENV=production iodine -p 3000 -t 4 -w 4
# vs.
$ RACK_ENV=production puma -p 3000 -t 4 -w 4
# Review the `iodine -?` help for more command line options.
```

It's recommended that the servers (Iodine/Puma) and the client (`wrk`/`ab`) run on separate machines.

It is worth noting that iodine can also speed up logging by replacing the logging middleware with `iodine -v`. This approach uses less memory and improves performance at the expense of fuzzy timing and some string caching.

On my machine, testing with the logging functionality enabled, iodine was more then 10 times faster than puma (60.9K req/sec vs. 5.3K req/sec)

### A few notes

Iodine's upgrade / callback design has a number of benefits, some of them related to better IO handling, resource optimization (no need for two IO polling systems), etc. This also allows us to use middleware without interfering with connection upgrades and provides backwards compatibility.

Iodine's HTTP server imposes a few restrictions for performance and security reasons, such as limiting each header line to 8Kb. These restrictions shouldn't be an issue and are similar to limitations imposed by Apache or Nginx.

If you still want to use Rack's `hijack` API, iodine will support you - but be aware that you will need to implement your own reactor and thread pool for any sockets you hijack, as well as a socket buffer for non-blocking `write` operations (why do that when you can write a protocol object and have the main reactor manage the socket?).

## Installation

To install iodine, simply install the the `iodine` gem:

```bash
$ gem install iodine
```

Iodine is written in C and allows some compile-time customizations, such as:

* `FIO_FORCE_MALLOC` - avoids iodine's custom memory allocator and use `malloc` instead (mostly used when debugging iodine or when using a different memory allocator).

* `FIO_MAX_SOCK_CAPACITY` - limits iodine's maximum client capacity. Defaults to 131,072 clients.

* `FIO_USE_RISKY_HASH` - replaces SipHash with RiskyHash for iodine's internal hash maps.
 
    Since iodine hash maps have internal protection against collisions and hash flooding attacks, it's possible for iodine to leverage RiskyHash, which is faster than SipHash.

    By default, SipHash will be used. This is a community related choice, since the community seems to believe a hash function should protect the hash map rather than it being enough for a hash map implementation to be attack resistance.

* `HTTP_MAX_HEADER_COUNT` - limits the number of headers the HTTP server will accept before disconnecting a client (security). Defaults to 128 headers (permissive).

* `HTTP_MAX_HEADER_LENGTH` - limits the number of bytes allowed for a single header (pre-allocated memory per connection + security). Defaults to 8Kb per header line (normal).

* `HTTP_BUSY_UNLESS_HAS_FDS` - requires at least X number of free file descriptors (for new database connections, etc') before accepting a new HTTP client.

* `FIO_ENGINE_POLL` - prefer the `poll` system call over `epoll` or `kqueue` (not recommended).

* `FIO_LOG_LENGTH_LIMIT` - sets the limit on iodine's logging messages (uses stack memory, so limits must be reasonable. Defaults to 2048.

* `FIO_TLS_PRINT_SECRET` - if true, the OpenSSL master key will be printed as debug message level log. Use only for testing (with WireShark etc'), never in production! Default: false.

These options can be used, for example, like so:

```bash
  gem install iodine -- \
      --with-cflags=\"-DHTTP_MAX_HEADER_LENGTH=48000 -DFIO_FORCE_MALLOC=1 -DHTTP_MAX_HEADER_COUNT=64\"
```

More possible compile time options can be found in the [facil.io documentation](http://facil.io).

## Evented oriented design with extra safety

Iodine is an evented server, similar in its architecture to `nginx` and `puma`. It's different than the simple "thread-per-client" design that is often taught when we begin to learn about network programming.

By leveraging `epoll` (on Linux) and `kqueue` (on BSD), iodine can listen to multiple network events on multiple sockets using a single thread.

All these events go into a task queue, together with the application events and any user generated tasks, such as ones scheduled by [`Iodine.run`](http://www.rubydoc.info/github/boazsegev/iodine/Iodine#run-class_method).

In pseudo-code, this might look like this

```ruby
QUEUE = Queue.new

def server_cycle
    if(QUEUE.empty?)
      QUEUE << get_next_32_socket_events # these events schedule the proper user code to run
    end
    QUEUE << server_cycle
end

def run_server
      while ((event = QUEUE.pop))
            event.shift.call(*event)
      end
end
```

In pure Ruby (without using C extensions or Java), it's possible to do the same by using `select`... and although `select` has some issues, it could work well for lighter loads.

The server events are fairly fast and fragmented (longer code is fragmented across multiple events), so one thread is enough to run the server including it's static file service and everything... 

...but single threaded mode should probably be avoided.


It's very common that the application's code will run slower and require external resources (i.e., databases, a custom pub/sub service, etc'). This slow code could "starve" the server, which is patiently waiting to run it's short tasks on the same thread.

The thread pool is there to help slow user code.

The slower your application code, the more threads you will need to keep the server running in a responsive manner (note that responsiveness and speed aren't always the same).

To make a thread pool easier and safer to use, iodine makes sure that no connection task / callback is called concurrently for the same connection.

For example, a is a WebSocket connection is already busy in it's `on_message` callback, no other messages will be forwarded to the callback until the current callback returns.

## Free, as in freedom (BYO beer)

Iodine is **free** and **open source**, so why not take it out for a spin?

It's installable just like any other gem on Ruby MRI, run:

```
$ gem install iodine
```

If building the native C extension fails, please note that some Ruby installations, such as on Ubuntu, require that you separately install the development headers (`ruby.h` and friends). I have no idea why they do that, as you will need the development headers for any native gems you want to install - so hurry up and get them.

If you have the development headers but still can't compile the iodine extension, [open an issue](https://github.com/boazsegev/iodine/issues) with any messages you're getting and I'll be happy to look into it.

## Mr. Sandman, write me a server

Iodine allows custom TCP/IP server authoring, for those cases where we need raw TCP/IP (UDP isn't supported just yet). 

Here's a short and sweet echo server - No HTTP, just use `telnet`:

```ruby
USE_TLS = false

require 'iodine'

# an echo protocol with asynchronous notifications.
class EchoProtocol
  # `on_message` is called when data is available.
  def on_message client, buffer
    # writing will never block and will use a buffer written in C when needed.
    client.write buffer
    # close will be performed only once all the data in the write buffer
    # was sent. use `force_close` to close early.
    client.close if buffer =~ /^bye[\r\n]/i
    # run asynchronous tasks... after a set number of milliseconds
    Iodine.run_after(1000) do
      # or schedule the task immediately
      Iodine.run do
        puts "Echoed data: #{buffer}"
      end
    end
  end
end

tls = USE_TLS ? Iodine::TLS.new("localhost") : nil

# listen on port 3000 for the echo protocol.
Iodine.listen(port: "3000", tls: tls) { EchoProtocol.new }
Iodine.threads = 1
Iodine.workers = 1
Iodine.start
```

Or a nice plain text chat room (connect using `telnet` or `nc` ):

```ruby
require 'iodine'

# a chat protocol with asynchronous notifications.
class ChatProtocol
  def initialize nickname = "guest"
    @nickname = nickname
  end
  def on_open client
    client.subscribe :chat
    client.publish :chat, "#{@nickname} joined chat.\n"
    client.timeout = 40
  end
  def on_close client
    client.publish :chat, "#{@nickname} left chat.\n"
  end
  def on_shutdown client
    client.write "Server is shutting down... try reconnecting later.\n"
  end
  def on_message client, buffer
    if(buffer[-1] == "\n")
      client.publish :chat, "#{@nickname}: #{buffer}"
    else
      client.publish :chat, "#{@nickname}: #{buffer}\n"
    end
    # close will be performed only once all the data in the outgoing buffer
    client.close if buffer =~ /^bye[\r\n]/i
  end
  def ping client
    client.write "(ping) Are you there, #{@nickname}...?\n"
  end
end

# an initial login protocol
class LoginProtocol
  def on_open client
    client.write "Enter nickname to log in to chat room:\n"
    client.timeout = 10
  end
  def ping client
    client.write "Time's up... goodbye.\n"
    client.close
  end
  def on_message client, buffer
    # validate nickname and switch connection callback to ChatProtocol
    nickname = buffer.split("\n")[0]
    while (nickname && nickname.length() > 0 && (nickname[-1] == '\n' || nickname[-1] == '\r'))
      nickname = nickname.slice(0, nickname.length() -1)
    end
    if(nickname && nickname.length() > 0 && buffer.split("\n").length() == 1)
      chat = ChatProtocol.new(nickname)
      client.handler = chat
    else
      client.write "Nickname error, try again.\n"
      on_open client
    end
  end
end

# listen on port 3000
Iodine.listen(port: 3000) { LoginProtocol.new }
Iodine.threads = 1
Iodine.workers = 1
Iodine.start
```

### Why not EventMachine?

EventMachine attempts to give the developer access to the network layer while Iodine attempts to abstract the network layer away and offer the developer a distraction free platform.

You can go ahead and use EventMachine if you like. They're doing amazing work on that one and it's been used a lot in Ruby-land... really, tons of good developers and people on that project.

But why not take iodine out for a spin and see for yourself?

## Can I contribute?

Yes, please, here are some thoughts:

* I'm really not good at writing automated tests and benchmarks, any help would be appreciated. I keep testing manually and that's less then ideal (and it's mistake prone).

* PRs or issues related to [the `facil.io` C framework](https://github.com/boazsegev/facil.io) should be placed in [the `facil.io` repository](https://github.com/boazsegev/facil.io).

* Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.

* If we can write a Java wrapper for [the `facil.io` C framework](https://github.com/boazsegev/facil.io), it would be nice... but it could be as big a project as the whole gem, as a lot of minor details are implemented within the bridge between these two languages.

* If you love the project or thought the code was nice, maybe helped you in your own project, drop me a line. I'd love to know.

### Running the Tests

Running this task will compile the C extensions then run RSpec tests:

```sh
bundle exec rake spec
```

## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).
