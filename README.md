[![Logo](logo.svg)](https://github.com/boazsegev/iodine)

[![POSIX CI](https://github.com/boazsegev/iodine/actions/workflows/posix.yml/badge.svg)](https://github.com/boazsegev/iodine/actions/workflows/posix.yml)
[![Windows CI](https://github.com/boazsegev/iodine/actions/workflows/Windows.yml/badge.svg)](https://github.com/boazsegev/iodine/actions/workflows/Windows.yml)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Gem](https://img.shields.io/gem/dt/iodine.svg)](https://rubygems.org/gems/iodine)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

# iodine - The Ruby Server/Client Combo You Wanted

Iodine is a fast concurrent Web Application Server/Client combination that's perfect for both micro-services and inter-connected monoliths.

* Iodine is a [Rack Application **Server**](examples/config.ru).
* Iodine is a [NeoRack Application **Server**](examples/config.nru).
* Iodine is an evented [TCP/IP, HTTP, WebSocket and SSE **Client**](examples/client).

Designed for your Real-Time and Event-Stream needs, Iodine boasts native support for:

* HTTP, WebSockets and EventSource (SSE) Services (server/client);
* Event-Stream Pub/Sub (with optional Redis Pub/Sub scaling);
* Hot Restarts and Hot Deployments;
* Static File Service (with automatic `.br`, `.gz`, and `.zip` support for pre-compressed assets);
* Performant Request Logging;
* Fast(!) builtin Mustache template render engine;
* Asynchronous Tasks and Timers (non-persistent memory cached);
* HTTP/1.1 keep-alive and pipeline throttling;
* Separate Memory Allocators for Heap Fragmentation Protection;
* TLS 1.2 and above (Requiring OpenSSL >= 3);
* and more!

Iodine is a Ruby wrapper for parts of the [facil.io](https://facil.io) C framework, leveraging the speed of C for many common web application tasks. In addition, iodine abstracts away all network concerns, so you never need to worry about the transport layer, leaving you free to concentrate on your application logic.

Leveraging the power of the [C facil.io framework](https://github.com/boazsegev/facil.io) for Ruby:

* Iodine can handle **tens of thousands of concurrent connections** (tested with more then 20K connections on Linux)! Limits depend on machine resources and app design, not on the server.

* Iodine is ideal for **Linux/Unix** based systems (i.e. macOS, Ubuntu, FreeBSD etc') and evented IO (while Windows and Solaris are better at IO *completion* events, which are very different).

Iodine is a C extension for Ruby, developed and optimized for the mainstream Ruby MRI.

## Security

Iodine was built with security in mind, making sure that clients will not have the possibility to abuse the server's resources. This includes limiting header line length, body payload sizes, WebSocket message length etc', as well as diverting larger HTTP payloads to temporary files.

please review the `iodine -h` command line options for more details on these and see if you need to change the defaults to fit better with your specific restrictions and use-cases.


## WebSockets Server/Client with Native Pub/Sub

Iodine includes a light and fast HTTP and WebSocket server (and client) written in C that was written to support both the [NeoRack Specifications](https://github.com/boazsegev/neorack) (with [WebSocket](https://github.com/boazsegev/neorack/blob/master/extensions/websockets.md) / [SSE](https://github.com/boazsegev/neorack/blob/master/extensions/sse.md)) and [Rack specifications](https://github.com/rack/rack/blob/main/SPEC.rdoc) (with the experimental [WebSocket / SSE Rack draft](https://github.com/boazsegev/neorack/blob/master/deprecated/Rack-WebSocket-Draft.md)).

Iodine also supports native process cluster Pub/Sub and a native RedisEngine to easily scale iodine's Pub/Sub horizontally.

## HTTP Streaming

Although Iodine supports HTTP Streaming, Streaming an HTTP response using a classical Rack approach (as in with Rails) is often a bad performance choice no matter the server you use.

Iodine with [NeoRack](https://github.com/boazsegev/neorack) attempts to offer a better approach for streaming, but at the end of the day, it would be better to avoid streaming when possible. If you plan to Stream anyway, strive for an evented approach rather than blocking a worker thread or using a classical Rack based application.

## Installing and Running Iodine

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
iodine
```

### TLS/SSL Configuration

Iodine supports two TLS backends:

1. **OpenSSL** (default when available) - Full-featured TLS 1.2+ support
2. **Embedded TLS 1.3** - Lightweight built-in implementation (always available)

#### Automatic Detection

Iodine automatically detects OpenSSL during installation and uses it as the default TLS backend. If OpenSSL is unavailable, the embedded TLS 1.3 implementation is used automatically.

#### Using Embedded TLS 1.3

The embedded TLS 1.3 implementation is always available and can be enabled in several ways:

**At Runtime (recommended):**
```ruby
# In your application code
Iodine::TLS.default = :iodine
```

**Via Environment Variable:**
```bash
# Set before starting iodine
export IODINE_MTLS=1
iodine
```

**Via Command Line:**
```bash
iodine -mtls
```

**At Compile Time (forces embedded as default):**
```bash
IODINE_USE_EMBEDDED_TLS=1 gem install iodine
```

#### Checking TLS Backend Availability

```ruby
require 'iodine'

# Check what's available
Iodine::TLS::OPENSSL_AVAILABLE   # => true/false
Iodine::TLS::EMBEDDED_AVAILABLE  # => true (always)
Iodine::TLS::SUPPORTED           # => true (always - at least embedded is available)

# Get/set the current default
Iodine::TLS.default              # => :openssl or :iodine
Iodine::TLS.default = :iodine    # Switch to embedded TLS 1.3
```

#### TLS Certificate Configuration

```bash
# Self-signed certificate (auto-generated)
iodine -tls

# Custom certificate
iodine -cert /path/to/cert.pem -key /path/to/key.pem

# With password-protected key
iodine -cert /path/to/cert.pem -key /path/to/key.pem -tls-pass "password"
```

**Note**: The embedded TLS 1.3 implementation has not been independently audited. For production use with high security requirements, OpenSSL is recommended.

### Reporting Issues and Known Issues

See the [GitHub Open Issues](https://github.com/boazsegev/iodine/issues) list for known issues and to report new issues.

PRs and issues related to [the facil.io C STL and framework](https://github.com/facil-io/cstl) should be directed to [the Proper C STL facil.io repository](https://github.com/facil-io/cstl).

### Optimizing Iodine's Concurrency

To get the most out of iodine, consider the amount of CPU cores available and the concurrency level the application requires. See if this works for your application or customize according to the application's needs.

Command line arguments allow easy access to different options, including concurrency levels. i.e., to set up 16 threads and 4 processes:

```bash
iodine -t 16 -w 4
```

By using negative values, Iodine can calculate a good enough concurrency model for different applications. Here are some machine dependent examples:

```bash
# fast applications
iodine -t 1 -w -2
# slower, CPU heavy applications
iodine -t 4 -w -4
# slower, IO bound applications
iodine -t 5 -w -1
```

Note that negative values are evaluated as "CPU Cores / abs(Value)". i.e., on an 8 core CPU machine, the number `-2` will produce 4 worker processes or threads.

The environment variables `THREADS` and `WORKERS` are automatically recognized when iodine is first required, allowing environment specific customization. i.e.:

```bash
export THREADS=4
export WORKERS=-2 # negative values are fractions of CPU cores.
iodine
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
      Iodine.workers = ENV.fetch("WEB_CONCURRENCY", -2).to_i if ENV["WEB_CONCURRENCY"]
    end
    ```

**Note**: command-line instructions (CLI) and environment variables are the recommended way for configuring iodine, allowing for code-less configuration updates.

### Logging

To enable performant HTTP request logging from the command line, use the `-v` (verbose) option:

```bash
iodine -v
```

Iodine offers a slight performance boost by caching the date and time Strings when answering multiple requests during the same time frame.

### Static Files and Assets

Iodine can send static file and assets directly, bypassing the Ruby layer completely.

Since the Ruby layer is unaware of these requests, logging can be performed by turning iodine's logger on (see above).

```bash
iodine -www /my/public/folder
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

## Native Pub/Sub with *optional* Redis scaling

Iodine's core, `facil.io` offers a native Pub/Sub implementation that can be scaled across machine boundaries using Redis.

The default implementation covers the whole process cluster, so a single cluster doesn't need Redis.

Once a single iodine process cluster isn't enough, horizontal scaling for the Pub/Sub layer is as simple as connecting iodine to Redis using the `-r <url>` from the command line. i.e.:

```bash
iodine -w -1 -t 8 -r redis://localhost:6379
```

### Redis Configuration

Redis can be configured via command line or programmatically:

**Command Line:**
```bash
# Basic Redis connection
iodine -r redis://localhost:6379

# With authentication
iodine -r redis://user:password@redis.example.com:6379

# With ping interval (seconds)
iodine -r redis://localhost:6379 -rp 30
```

**Programmatic:**
```ruby
# Create Redis engine
redis = Iodine::PubSub::Engine::Redis.new("redis://localhost:6379/", ping: 30)

# Set as default Pub/Sub engine
Iodine::PubSub.default = redis

# Now all Iodine.publish calls will go through Redis
Iodine.publish(channel: "chat", message: "Hello from Ruby!")
```

### Using Redis for Direct Commands

The Redis engine also supports sending arbitrary Redis commands:

```ruby
redis = Iodine::PubSub::Engine::Redis.new("redis://localhost:6379/")

# Send Redis commands with async callbacks
redis.cmd("SET", "mykey", "Hello, Redis!") { |result| puts result }
redis.cmd("GET", "mykey") { |value| puts "Value: #{value}" }
redis.cmd("KEYS", "*") { |keys| p keys }
redis.cmd("INCR", "counter") { |new_value| puts new_value }
```

**Note**: Do not use `SUBSCRIBE`, `PSUBSCRIBE`, `UNSUBSCRIBE`, or `PUNSUBSCRIBE` commands directly. These are handled internally by the Pub/Sub system.

### Pub/Sub Decentralized Horizontal Scaling

Iodine can auto-detect other iodine machines on the same local network (using UDP publishing for address `0.0.0.0`).

This allows iodine to scale horizontally even without Redis.

To do so, all machines running iodine must share the same pub/sub port and secret.

Setting the secret can be performed using the `SECRET` environment variable, or the command line (`-scrt`).

Setting the pub/sub public port can be performed using the `PUBSUB_PORT` environment variable, or the command line (`-bp`).

Pub/Sub messages are encrypted using ChaCha20/Poly1305 and the shared secret, mitigating the risk of sensitive data leaking. However, note that if the machine itself is shared than the secret might be readable by those sharing the machine (depending how it is stored).

**Note** this feature is difficult to test and I don't test it regularly. Therefore, please let me know if you notice anything come up (i.e., dropped messages, isolated machines, whatever).

#### Pub/Sub Details and Limitations

Iodine's internal Pub/Sub Letter Exchange Protocol (inherited from facil.io) imposes the following limitations on message exchange:

* Distribution Channel Names are limited to 2^16 bytes (65,536 bytes).

* Message payload is limited to 2^24 bytes (16,777,216 bytes == about 16Mb).

* Empty messages (no numerical filters, no channel, no message payload, no flags) are ignored.

* Message delivery requires a match for both the channel name (or pattern) and the numerical filter (if none provided, zero is used).

Redis Support Notes:

* Iodine's Redis client does *not* support multiple databases. This is both because [database scoping is ignored by Redis during pub/sub](https://redis.io/topics/pubsub#database-amp-scoping) and because [Redis Cluster doesn't support multiple databases](https://redis.io/topics/cluster-spec). This indicated that multiple database support just isn't worth the extra effort and performance hit.

* The iodine Redis client uses two Redis connections per process (a publishing connection and a subscription connection), minimizing the Redis load and network bandwidth.

* Connections are automatically re-established if timeouts or errors occur.

* The Redis engine supports authentication via URL (e.g., `redis://user:password@host:port`).

## Hot Restart / Hot Deployments

Iodine will "hot-restart" the application by shutting down and re-spawning the worker processes, reloading all the gems (except `iodine` itself) and the application code along the way.

This will clear away any memory fragmentation concerns and other issues that might plague a long running worker process or ruby application.

### How to Hot Restart

To hot-restart iodine, send the `SIGUSR1` signal to the root process or to restart a single process (may result in multiple versions of the code running) signal `SIGINT` to a worker process.

The following code will hot-restart iodine every 4 hours when iodine is running in cluster mode:

```ruby
Iodine.run_every(4 * 60 * 60 * 1000) do
  Process.kill("SIGUSR1", Process.pid) unless Iodine.worker?
end
```

### How does Hot Restart Work?

This will only work with cluster mode (even if using only 1 worker, but not when using 0 workers).

The main process schedules new workers to spawn and signals the old ones to shut down. The code is (re)loaded by the worker (child) processes (the main process never loads the app). Any other option would have resulted in code artifacts during the upgrade.

The old workers won't accept new connections but will complete any existing requests and may even continue to respond to existing clients if they already pipelined their requests. These responses will use the old version of the app.

The new workers will reload the app code (including reloading all the of gems except for `iodine` itself) and start accepting and responding to new clients.

You will have both groups of workers with both versions of your code running for a short amount of time while older clients are served, but once the rotation is complete you should be running only the new code (and workers).

**Caveats**: 

- It's important to note that slower clients that hadn't sent their full request will be disconnected. This is a side effect I didn't address for security reasons. I did not wish to allow maliciously slow clients to perpetually block the child process from restarting.

- Also, a child that doesn't finish processing and sending the response within 15 seconds will be terminated without the full response being sent (this is controlled by the `FIO_IO_SHUTDOWN_TIMEOUT` compilation flag that defaults to `15000` milliseconds). This, again, is a malicious slow client concern, but also imposes some requirements on the web app code.

### Optimizing for Memory Instead

Using the `--preload` or `-warmup` options will disable hot code swapping and save memory by loading the application to the root process (leveraging the copy-on-write memory OS feature). It will also disable any ability to update the app without restarting iodine (useful, e.g., when using a container and load balancer for hot restarts).

## How does it compare to other servers?

Although Puma significantly improved since the first Iodine release, my tests show that Iodine is still significantly faster both in terms or latency and requests per second.

In my tests I avoided using NeoRack, as it wouldn't be fair. NeoRack by itself adds a significant performance boost due its design. For example, NeoRack it minimizes conversions between data formats and uses shorter String data (i.e., doesn't append `HTTP_` to header names).

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
