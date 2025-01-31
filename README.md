# iodine - The One with the Event Stream and Native WebSockets

[![POSIX CI](https://github.com/boazsegev/iodine/actions/workflows/posix.yml/badge.svg)](https://github.com/boazsegev/iodine/actions/workflows/posix.yml)
[![Windows CI](https://github.com/boazsegev/iodine/actions/workflows/Windows.yml/badge.svg)](https://github.com/boazsegev/iodine/actions/workflows/Windows.yml)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Gem](https://img.shields.io/gem/dt/iodine.svg)](https://rubygems.org/gems/iodine)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

[![Logo](https://github.com/boazsegev/iodine/raw/master/logo.png)](https://github.com/boazsegev/iodine)

## The Ruby Server You Wanted

Iodine is a fast concurrent Web Application Server for your Real-Time and Event-Stream needs, with native support for WebSockets and Pub/Sub services - but it's also so much more.

Iodine includes native support for:

* HTTP, WebSockets and EventSource (SSE) Services (server/client);
* Event-Stream Pub/Sub (with optional Redis Pub/Sub scaling);
* Fast(!) builtin Mustache template render engine;
* Static File Service (with automatic `.gz`, `.br` and `.zip` support for pre-compressed assets);
* Performant Request Logging;
* Asynchronous Tasks and Timers (memory cached);
* HTTP/1.1 keep-alive and pipeline throttling;
* Separate Memory Allocators for Heap Fragmentation Protection;
* TLS 1.2 and above (Requiring OpenSSL >= 3);
* and more!

Iodine is a Ruby wrapper for much of the [facil.io](https://facil.io) C framework, leveraging the speed of C for many common web application tasks. In addition, iodine abstracts away all network concerns, so you never need to worry about the transport layer, leaving you free to concentrate on your application logic.

Since iodine wraps much of the [C facil.io framework](https://github.com/boazsegev/facil.io) for Ruby:

* Iodine can handle **thousands of concurrent connections** (tested with more then 20K connections on Linux)! Limits depend on machine resources rather than software.

* Iodine is ideal for **Linux/Unix** based systems (i.e. macOS, Ubuntu, FreeBSD etc') and evented IO (while Windows and Solaris are better at IO *completion* events, which are very different).

Iodine is a C extension for Ruby, developed and optimized for Ruby MRI 2.3 and up... it should support the whole Ruby 2.x and 3.x MRI family, but CI tests are often limited to stable releases that are still maintained.

**Note**:

Streaming a Ruby response is often a bad performance choice no matter the server you use. [NeoRack](https://github.com/boazsegev/neorack) attempts to offer a better approach for streaming, but at the end of the day, it would be better to avoid streaming when possible. If you plan to Stream anyway, consider using [NeoRack](https://github.com/boazsegev/neorack) with an evented approach rather than blocking the thread (which is what happens when `each` is called in Ruby).

## Security

Iodine was built with security in mind, making sure that clients will not have the possibility to abuse the server's resources. This includes limiting header line length, body payload sizes, WebSocket message length etc', as well as diverting larger HTTP payloads to temporary files.

please review the `iodine -h` command line options for more details on these and see if you need to change the defaults to fit better with your specific restrictions and use-cases.

## Iodine - a fast & powerful HTTP + WebSockets server/client with native Pub/Sub

Iodine includes a light and fast HTTP and Websocket server written in C that was written to support both the [NeoRack](https://github.com/boazsegev/neorack) and [Rack interface specifications](http://www.rubydoc.info/github/rack/rack/master/file/SPEC) and the [WebSocket](https://github.com/boazsegev/neorack/blob/master/extensions/websockets.md) and [SSE]](https://github.com/boazsegev/neorack/blob/master/extensions/sse.md) extension draft.

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
gem 'iodine', '~>0.8'
```

Then start your application from the command-line / terminal using iodine:

```bash
bundler exec iodine
```

#### Installing with TLS/SSL

Iodine should automatically detect when Ruby was installed with OpenSSL and link against that same library.

Iodine will always respect encryption requirements, even if no library is available.

Requiring Iodine to use TLS when unavailable will result in Iodine crashing with an error message.


### Optimizing Iodine's Concurrency

To get the most out of iodine, consider the amount of CPU cores available and the concurrency level the application requires.

Iodine will calculate, when possible, a good enough default concurrency model for fast applications. See if this works for your application or customize according to the application's needs.

Command line arguments allow easy access to different options, including concurrency levels. i.e., to set up 16 threads and 4 processes:

```bash
bundler exec iodine -t 16 -w 4
```

The environment variables `THREADS` and `WORKERS` are automatically recognized when iodine is first required, allowing environment specific customization. i.e.:

```bash
export THREADS=4
export WORKERS=-2 # negative values are fractions of CPU cores.
bundler exec iodine
```

Negative values are evaluated as "CPU Cores / abs(Value)". i.e., on an 8 core CPU machine, this will produce 4 worker processes with 2 threads per worker:

```bash
bundler exec iodine -t 2 -w -2
```

### Running with Rails

On Rails:

1. Add `gem "iodine", "~> 0.8"` to your `Gemfile` (and comment out the `puma` gem).

1. Remove the `config/puma.rb` file (or make sure the code is conditional).

1. Optionally, it's possible to add a `config/initializers/iodine.rb` file. For example:

    ```ruby
    # Iodine setup - use conditional setup to make it easy to test other servers such as Puma:
    if(defined?(Iodine))
      Iodine.threads = ENV.fetch("RAILS_MAX_THREADS", 5).to_i if ENV["RAILS_MAX_THREADS"]
      Iodine.workers = ENV.fetch("WEB_CONCURRENCY", 2).to_i if ENV["WEB_CONCURRENCY"]
    end
    ```

**Note**: command-line instructions (CLI) and environment variables are the recommended way for configuring iodine, allowing for code-less configuration updates.

### Logging

To enable performant HTTP request logging from the command line, use the `-v` (verbose) option:

```bash
bundler exec iodine -p $PORT -t 16 -w -2 -www /my/public/folder -v
```

Iodine will cache the date and time String data when answering multiple requests during the same time frame, improving performance by minimizing system calls.

### Static Files and Assets

Iodine can send static file and assets directly, bypassing the Ruby layer completely.

This means that Iodine won't lock Ruby's GVL when serving static files.

Since the Ruby layer is unaware of these requests, logging can be performed by turning iodine's logger on (see above).

To use native static file service, setup the public folder's address **before** starting the server.

This can be done when starting the server either using the Ruby API or from the command line:

```bash
bundler exec iodine -t 16 -w 4 -www /my/public/folder
```

Iodine will automatically test for missing extension file names, such as `.html`, `.htm`, `.txt`, and `.md`, as well as a missing `index` file name when `path` points to a folder.

#### Pre-Compressed assets / files

Iodine will automatically recognize and send the compressed version of a static file (`.gz`, `.br`, `.zip`) if the client (browser) supports the compressed transfer-encoding.

For example, to offer a compressed version of `style.css`, run (in the terminal):

```bash
gzip -k -9 style.css
```

This results in both files, `style.css` (the original) and `style.css.gz` (the compressed).

When a browser that supports compressed encoding requests the file (and most browsers do), iodine will recognize that a pre-compressed option exists and will prefer the `gzip` compressed version.

It's as easy as that. No extra code required.

### Native HTTP `Upgrade` and SSE support

Iodine's HTTP server has native support for [WebSocket](https://github.com/boazsegev/neorack/blob/master/extensions/websockets.md)/[SSE](https://github.com/boazsegev/neorack/blob/master/extensions/sse.md), using both [NeoRack](https://github.com/boazsegev/neorack) extensions and the Rack `env` response style.

This promotes separation of concerns, where iodine handles all the Network related logic and the application can focus on the API and data it provides.

Of course, Hijacking the socket is still possible (for now), but highly discouraged.

#### Using Rack for WebSocket/SSE

With Rack, the `env['rack.upgrade?']` will be set to either `:websocket` or `:sse`, allowing the Rack application to either distinguish or unify the behavior desired. This is a simple broadcasting example:

```ruby
module App
  def self.call(env)
    txt = []
    if env['rack.upgrade?']
      env['rack.upgrade'] = self
    else
      env.each {|k,v| txt << "#{k}: #{v}\r\n" }
    end
    [200, {}, txt]
  end
  def self.on_open(e)
    e.subscribe :broadcast
  end
  def self.on_message(e, m)
    Iodine.publish :broadcast, m
  end
end
run App
```

#### Using NeoRack for WebSocket/SSE

The [NeoRack WebSocket](https://github.com/boazsegev/neorack/blob/master/extensions/websockets.md) and [SSE](https://github.com/boazsegev/neorack/blob/master/extensions/sse.md) specification drafts offers much more control over the path taken by a WebSocket request.

This control is available also to Rack Applications if they implement the proper callback methods(in addition to the `call` method).

i.e.:

```ruby
module MyNeoRackApp
  # this is fairly similar to the Rack example above.
  def self.on_http(e)
    out =  "path:    #{e.path}\r\nquery:   #{e.query}\r\n"
    out += "method:  #{e.method}\r\nversion: #{e.version}\r\n"
    out += "from:    #{e.from} (#{e.peer_addr})\r\n"
    e.headers.each {|k,v| out += "#{k}: #{v}\r\n" }
    # echo request body to the response
    while(l = e.gets)
      out += l
    end
    # write the data and finish (this should avoid streaming overhead).
    e.finish out 
  end

  # basically the default implementation when either `on_open`/`on_message` are defined.
  def self.on_authenticate(e)
    true
  end
  # called when either an SSE or a WebSocket connection is open.
  def self.on_open(e)
    e.subscribe :broadcast
  end
  # Called an Event Source (SSE) client send a re-connection request with the ID of the last message received.
  def self.on_eventsource_reconnect(sse, last_message_id)
    puts "Reconnecting SSE client #{sse.object_id}, should send everything after message ID #{last_message_id}"
  end
  # Only WebSocket connection should be able to receive messages from clients.
  def self.on_message(e, m)
    Iodine.publish :broadcast, m
  end
  # Iodine allows clients to send properly formatted events, should you want to cheat a little.
  # This is usually more useful when using Iodine as an SSE client.
  def self.on_eventsource(sse, message)
    puts "Normally only an SSE client would receive sse messages... See Iodine::Connection.new\r\n"
    puts "id: #{message.id}"
    puts "event: #{message.event}"
    puts "data: #{message.data}"
  end
end

run MyNeoRackApp
```

#### Iodine as a WebSocket/SSE Client

Iodine can also attempt to connect to an external server as a WebSocket or SSE client.

Simply call `Iodine::Connection.new(url, handler: MyClient)` using the same callback methods defined in the NeoRack [WebSocket](https://github.com/boazsegev/neorack/blob/master/extensions/websockets.md) and [SSE](https://github.com/boazsegev/neorack/blob/master/extensions/sse.md) specifications.

There's an example `client` CLI application in the examples folder.

### Native Pub/Sub with *optional* Redis scaling

**Note**: not yet implemented in versions `0.8.x`, but the documentation hadn't been removed with hopes of a soon-to-be implementation.

Iodine's core, `facil.io` offers a native Pub/Sub implementation that can be scaled across machine boundaries using Redis.

The default implementation covers the whole process cluster, so a single cluster doesn't need Redis

Once a single iodine process cluster isn't enough, horizontal scaling for the Pub/Sub layer is as simple as connecting iodine to Redis using the `-r <url>` from the command line. i.e.:

```bash
iodine -w -1 -t 8 -r redis://localhost
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

#### Pub/Sub Details and Limitations

Iodine's internal Pub/Sub Letter Exchange Protocol (inherited from facil.io) imposes the following limitations on message exchange:

* Distribution Channel Names are limited to 2^16 bytes (65,536 bytes).

* Message payload is limited to 2^24 bytes (16,777,216 bytes == about 16Mb).

* Empty messages (no numerical filters, no channel, no message payload, no flags) are ignored.

* Subscriptions match delivery matches by both channel name (or pattern) and the numerical filter.

Redis Support Limitations:

* Redis support **has yet to be implemented** in the Iodine `0.8.x` versions.

  Note: It is possible that it won't make it into the final release, as Redis is less OpenSource than it was and I just don't have the desire to code for something I stopped using... but I would welcome a PR implementing this in the [facil.io C STL repo](https://github.com/facil-io/cstl).

* Iodine's Redis client does *not* support multiple databases. This is both because [database scoping is ignored by Redis during pub/sub](https://redis.io/topics/pubsub#database-amp-scoping) and because [Redis Cluster doesn't support multiple databases](https://redis.io/topics/cluster-spec). This indicated that multiple database support just isn't worth the extra effort and performance hit.

* The iodine Redis client will use two Redis connections for each process cluster (a single publishing connection and a single subscription connection), minimizing the Redis load and network bandwidth.

* Connections will be automatically re-established if timeouts or errors occur.

### Hot Restart / Hot Updates

Iodine will "hot-restart" the application by shutting down and re-spawning the worker processes, reloading all the gems (except `iodine` itself) and the application code along the way.

This will clear away any memory fragmentation concerns and other issues that might plague a long running worker process or ruby application.

#### How to Hot Restart

To hot-restart iodine, send the `SIGUSR1` signal to the root process or to restart a single process (may result in multiple versions of the code running) signal `SIGINT` to a worker process.

The following code will hot-restart iodine every 4 hours when iodine is running in cluster mode:

```ruby
Iodine.run_every(4 * 60 * 60 * 1000) do
  Process.kill("SIGUSR1", Process.pid) unless Iodine.worker?
end
```

#### How does Hot Restart Work?

This will only work with cluster mode (even if using only 1 worker, but not when using 0 workers).

The main process schedules new workers to spawn and signals the old ones to shut down. The code is (re)loaded by the worker (child) processes (the main process never loads the app). Any other option would have resulted in code artifacts during the upgrade.

The old workers won't accept new connections but will complete any existing requests and may even continue to respond to existing clients if they already pipelined their requests. These responses will use the old version of the app.

The new workers will reload the app code (including reloading all the of gems except for `iodine` itself) and start accepting and responding to new clients.

You will have both groups of workers with both versions of your code running for a short amount of time while older clients are served, but once the rotation is complete you should be running only the new code (and workers).

**Caveats**: 

- It's important to note that slower clients that hadn't sent their full request will be disconnected. This is a side effect I didn't address for security reasons. I did not wish to allow maliciously slow clients to perpetually block the child process from restarting.

- Also, a child that doesn't finish processing and sending the response within 15 seconds will be terminated without the full response being sent (this is controlled by the `FIO_IO_SHUTDOWN_TIMEOUT` compilation flag that defaults to `15000` milliseconds). This, again, is a malicious slow client concern, but also imposes some requirements on the web app code.

#### Optimizing for Memory Instead

Using the `--preload` or `-warmup` options will disable hot code swapping and save memory by loading the application to the root process (leveraging the copy-on-write memory OS feature). It will also disable any ability to update the app without restarting iodine (useful, e.g., when using a container and load balancer for hot restarts).

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
Iodine.workers = 0
Iodine::Connection.new "wss://echo.websocket.org", handler: EchoClient.new, ping: 40
Iodine.start
```

### How does it compare to other servers?

Although Puma significantly improved since the first Iodine release, my tests show that Iodine is still significantly faster both in terms or latency and requests per second.

In my tests I avoided using NeoRack, as it wouldn't be fair. NeoRack by itself adds a significant performance boost due its design. For example, NeoRack it minimizes conversions between data formats (i.e., we don't append `HTTP_` to header names).

I am excited to have you test it for yourself - even better if you test performance using your own application and a number of possible different settings (how many threads per CPU core? how many worker processes? middleware vs. server request logging, etc').

I recommend benchmarking the performance for yourself using tools such as `wrk`, i.e.:

```bash
$ wrk -c200 -d4 -t2 http://localhost:3000/
```

The best application to use for benchmarking is your actual application.

You could create a simple `config.ru` file with a **hello world** app, and even though this will really showcase the server's performance, it probably won't matter for your specific use-case:

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
$ RACK_ENV=production iodine -p 3000 -t 4 -w 4 -v
# vs.
$ RACK_ENV=production puma -p 3000 -t 4 -w 4 -v
# Review the `iodine -h` help for more command line options.
```

It's recommended that the servers (Iodine/Puma) and the client (i.e. `wrk`) run on separate machines.

## Installation

To install iodine, simply install the the `iodine` gem:

```bash
gem install iodine
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

## Free, as in freedom (BYO beer)

Iodine is **free** and **open source**, so why not take it out for a spin?

It's installable just like any other gem on Ruby MRI, run:

```
gem install iodine
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
  def self.on_message client, buffer
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
Iodine.listen(service: :raw, tls: tls, handler: EchoProtocol)
Iodine.threads = 1
Iodine.workers = 1
Iodine.start
```

Or a nice plain text chat room (connect using `telnet` or `nc` ):

```ruby
require 'iodine'

# a chat protocol with asynchronous notifications.
module ChatProtocol
  def self.on_open client
    puts "Connecting #{client[:nickname]} to Chat"
    client.subscribe :chat
    client.publish :chat, "#{client[:nickname]} joined chat.\n"
  end
  def self.on_close client
    client.publish :chat, "#{client[:nickname]} left chat.\n"
    puts "Disconnecting #{client[:nickname]}."
  end
  def self.on_shutdown client
    client.write "Server is shutting down... try reconnecting later.\n"
  end
  def self.on_message client, buffer
    if(buffer[-1] == "\n")
      client.publish :chat, "#{client[:nickname]}: #{buffer}"
    else
      client.publish :chat, "#{client[:nickname]}: #{buffer}\n"
    end
    # close will be performed only once all the data in the outgoing buffer
    client.close if buffer =~ /^bye[\r\n]/i
  end
  def self.on_timeout client
    client.write "(ping) Are you there, #{client[:nickname]}...?\n"
  end
end

# an initial login protocol
module LoginProtocol
  def self.on_open client
    puts "Accepting new Client"
    client.write "Enter nickname to log in to chat room:\n"
  end
  def self.on_timeout client
    client.write "Time's up... goodbye.\n"
    client.close
  end
  def self.on_message client, buffer
    # validate nickname and switch connection callback to ChatProtocol
    nickname = buffer.split("\n")[0]
    while (nickname && nickname.length() > 0 && (nickname[-1] == '\n' || nickname[-1] == '\r'))
      nickname = nickname.slice(0, nickname.length() -1)
    end
    if(nickname && nickname.length() > 0 && buffer.split("\n").length() == 1)
      client[:nickname] = nickname
      client.handler = ChatProtocol
      client.handler.on_open(client)
    else
      client.write "Nickname error, try again.\n"
      on_open client
    end
  end
end

# listen on port 3000
Iodine.listen(url: 'tcp://0.0.0.0:3000', handler: LoginProtocol, timeout: 40)
Iodine.threads = 1
Iodine.workers = 0
Iodine.start
```

### Heap Fragmentation Protection

Iodine includes a fast, network oriented, custom memory allocator, optimizing away some of the work usually placed on the Ruby Garbage Collector (GC).

This approach helps to minimize heap fragmentation for long running processes, by grouping many short-lived objects into a common memory space.

It is still recommended to consider [jemalloc](http://jemalloc.net) or other allocators that also help mitigate heap fragmentation issues.


## Can I contribute?

Yes, please, here are some thoughts:

* I'm really not good at writing automated tests and benchmarks, any help would be appreciated. I keep testing manually and that's less then ideal (and it's mistake prone).

* PRs or issues related to [the `facil.io` C framework](https://github.com/boazsegev/facil.io) should be placed in [the `facil.io` repository](https://github.com/boazsegev/facil.io).

* Bug reports and pull requests are welcome on GitHub at <https://github.com/boazsegev/iodine>.

* If we can write a Java wrapper for [the `facil.io` C framework](https://github.com/boazsegev/facil.io), it would be nice... but it could be as big a project as the whole gem, as a lot of minor details are implemented within the bridge between these two languages.

* If you love the project or thought the code was nice, maybe helped you in your own project, drop me a line. I'd love to know.

### Running the Tests

Running this task will compile the C extensions then run RSpec tests:

```sh
bundle exec rake spec
```

## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).
