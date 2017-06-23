# iodine - HTTP / Websocket Server with Pub/Sub support, optimized for Ruby MRI on Linux / BSD
[![Logo](https://github.com/boazsegev/iodine/raw/master/logo.png)](https://github.com/boazsegev/iodine)

[![Build Status](https://travis-ci.org/boazsegev/iodine.svg?branch=master)](https://travis-ci.org/boazsegev/iodine)
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

Iodine is a fast concurrent web server for real-time Ruby applications, with native support for:

* Websockets;
* Pub/Sub (with optional Redis Pub/Sub scaling);
* Static file service (with automatic `gzip` support for pre-compressed versions);
* HTTP/1.1 keep-alive and pipelining;
* Asynchronous event scheduling and timers;
* Client connectivity (attach client sockets to make them evented);
* Custom protocol authoring;
* and more!

Iodine is an **evented** framework with a simple API that builds off the low level [C code library facil.io](https://github.com/boazsegev/facil.io) with support for **epoll** and **kqueue** - this means that:

* Iodine can handle **thousands of concurrent connections** (tested with more then 20K connections)!

* Iodine supports only **Linux/Unix** based systems (i.e. OS X, Ubuntu, FreeBSD etc'), which are ideal for evented IO (while Windows and Solaris are better at IO *completion* events, which are totally different).

Iodine is a C extension for Ruby, developed and optimized for Ruby MRI 2.2.2 and up... it should support the whole Ruby 2.0 MRI family, but Rack requires Ruby 2.2.2, and so Iodine matches this requirement.

## Iodine::Rack == a fast and powerful HTTP + Websockets server with native Pub/Sub

Iodine includes a light and fast HTTP and Websocket server written in C that was written according to the [Rack interface specifications](http://www.rubydoc.info/github/rack/rack/master/file/SPEC) and the [Websocket draft extension](./SPEC-Websocket-Draft.md).

With `Iodine.listen2http` it's possible to run multiple HTTP applications in addition to (or instead of) the default `Iodine::Rack` HTTP service.

Iodine also supports native process cluster Pub/Sub and a native RedisEngins to easily scale Iodine's Pub/Sub horizontally.

### Running the web server

Using the Iodine server is easy, simply add Iodine as a gem to your Rack application:

```ruby
gem 'iodine', '~>0.4'
```

Iodine will calculate, when possible, a good enough default concurrency model for lightweight applications... this might not fit your application if you use heavier database access or other blocking calls.

To get the most out of Iodine, consider the amount of CPU cores available and the concurrency level the application requires.

The common model of 16 threads and 4 processes can be easily adopted:

```bash
bundler exec iodine -p $PORT -t 16 -w 4
```

### Static file serving support

Iodine supports an internal static file service that bypasses the Ruby layer  and serves static files directly from "C-land".

This means that Iodine won't lock Ruby's GVL when sending static files. The files will be sent directly, allowing for true native concurrency.

Since the Ruby layer is unaware of these requests, logging can be performed by turning iodine's logger on.

To use native static file service, setup the public folder's address **before** starting the server.

This can be done when starting the server from the command line:

```bash
bundler exec iodine -p $PORT -t 16 -w 4 -www /my/public/folder
```

Or by adding a single line to the application. i.e. (a `config.ru` example):

```ruby
require 'iodine'
Iodine::Rack.public = '/my/public/folder'
out = [404, {"Content-Length" => "10".freeze}.freeze, ["Not Found.".freeze].freeze].freeze
app = Proc.new { out }
run app
```

To enable logging from the command line, use the `-v` (verbose) option:

```bash
bundler exec iodine -p $PORT -t 16 -w 4 -www /my/public/folder -v
```

#### X-Sendfile

Ruby can leverage static file support (if enabled) by using the `X-Sendfile` header in the Ruby application response.

This allows Ruby to send very large files using a very small memory footprint, as well as (when possible) leveraging the `sendfile` system call.

i.e. (example `config.ru` for Iodine):

```ruby
app = proc do |env|
  request = Rack::Request.new(env)
  if request.path_info == '/source'.freeze
    [200, { 'X-Sendfile' => File.expand_path(__FILE__) }, []]
  elsif request.path_info == '/file'.freeze
    [200, { 'X-Header' => 'This was a Rack::Sendfile response sent as text.' }, File.open(__FILE__)]
  else
    [200, { 'Content-Type'.freeze => 'text/html'.freeze,
            'Content-Length'.freeze => request.path_info.length.to_s },
     [request.path_info]]
 end
end
# # optional:
# use Rack::Sendfile
run app
```

Go to [localhost:3000/source](http://localhost:3000/source) to download the `config.ru` file using the `X-Sendfile` extension.

#### Pre-Compressed assets / files

Simply `gzip` your static files and iodine will automatically recognize and send the `gz` version if the client (browser) supports the `gzip` transfer-encoding.

For example, to offer a compressed version of `style.css`, run (in the terminal):

      $  gzip -k -9 style.css

Now, you will have two files in your folder, `style.css` and `style.css.gz`.

When a browser that supports compressed encoding (which is most browsers) requests the file, iodine will recognize that a pre-compressed option exists and will prefer the `gzip` compressed version.

It's as easy as that. No extra code required.

### Special HTTP `Upgrade` support

Iodine's HTTP server includes special support for the Upgrade directive using Rack's `env` Hash, allowing the application to focus on services and data while Iodine takes care of the network layer.

Upgrading an HTTP connection can be performed either using Iodine's Websocket Protocol support with `env['upgrade.websocket']` or by implementing your own protocol directly over the TCP/IP layer - be it a websocket flavor or something completely different - using `env['upgrade.tcp']`.

#### Websockets

When an HTTP Upgrade request is received, Iodine will set the Rack Hash's upgrade property to `true`, so that: `env[upgrade.websocket?] == true`

To "upgrade" the HTTP request to the Websockets protocol, simply provide Iodine with a Websocket Callback Object instance or class: `env['upgrade.websocket'] = MyWebsocketClass` or `env['upgrade.websocket'] = MyWebsocketClass.new(args)`

Iodine will adopt the object, providing it with network functionality (methods such as `write`, `each`, `defer` and `close` will become available) and invoke it's callbacks on network events.

Here is a simple chatroom example we can run in the terminal (`irb`) or easily paste into a `config.ru` file:

```ruby
require 'iodine'
class WebsocketChat
  def on_open
    # Pub/Sub directly to the client (or use a block to process the messages)
    subscribe channel: :chat
    # Writing directly to the socket
    write "You're now in the chatroom."
  end
  def on_message data
    # Strings and symbol channel names are equivalent.
    publish channel: "chat", message: data
  end
end
Iodine::Rack.app= Proc.new do |env|
  if env['upgrade.websocket?'.freeze] && env["HTTP_UPGRADE".freeze] =~ /websocket/i.freeze
    env['upgrade.websocket'.freeze] = WebsocketChat # or: WebsocketChat.new
    [0,{}, []] # It's possible to set cookies for the response.
  else
    [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
  end
end
# Pus/Sub can be server oriented as well as connection bound
root_pid = Process.pid
Iodine.subscribe(channel: :chat) {|ch, msg| puts msg if Process.pid == root_pid }
# By default, Pub/Sub performs in process cluster mode.
Iodine.processes = 4
# static file serving can be set manually as well as using the command line:
Iodine::Rack.public = "www/public"
#
Iodine.start
```

#### Native Pub/Sub with *optional* Redis scaling

Iodine's core, `facil.io` offers a native Pub/Sub implementation. The implementation is totally native to iodine, it covers the whole process cluster and it can be easily scaled by using Redis (which isn't required except for horizontal scaling).

Here's an example that adds horizontal scaling to the chat application in the previous example, so that Pub/Sub messages are published across many machines at once:

```ruby
require 'uri'
# initialize the Redis engine for each Iodine process.
if ENV["REDIS_URL"]
  uri = URI(ENV["REDIS_URL"])
  Iodine.default_pubsub = Iodine::PubSub::RedisEngine.new(uri.host, uri.port, 0, uri.password)
else
  puts "* No Redis, it's okay, pub/sub will still run on the whole process cluster."
end

# ... the rest of the application remain unchanged.
```

The new Redis client can also be used for asynchronous Redis command execution. i.e.:

```ruby
if(Iodine.default_pubsub.is_a? Iodine::PubSub::RedisEngine)
  # Ask Redis about all it's client connections and print out the reply.
  Iodine.default_pubsub.send("CLIENT LIST") { |reply| puts reply }
end
```

**Notice:**

Iodine does not use a Hash table for the Pub/Sub channels, it uses a [4 bit trie](https://en.wikipedia.org/wiki/Trie).

The cost is higher memory consumption per channel and a limitation of 1024 bytes per channel name (shorter names are better).

The bonus is high lookup times, zero chance of channel conflicts and an optimized preference for shared prefix channels (i.e. "user:1", "user:2"...).

Another added bonus is pattern publishing (is addition to pattern subscriptions) which isn't available when using Redis (since Redis doesn't support this feature).


#### TCP/IP (raw) sockets

Upgrading to a custom protocol (i.e., in order to implement your own Websocket protocol with special extensions) is performed almost the same way, using `env['upgrade.tcp']`. In the following (terminal) example, we'll use an echo server without direct socket echo:

```ruby
require 'iodine'
class MyProtocol
  def on_message data
    # regular socket echo - NOT websockets - notice the upgrade code
    write data
  end
end
Iodine::Rack.app = Proc.new do |env|
  if env['upgrade.tcp?'.freeze] && env["HTTP_UPGRADE".freeze] =~ /echo/i.freeze
    env['upgrade.tcp'.freeze] = MyProtocol
    # no HTTP response will be sent when the status code is 0 (or less).
    # to upgrade AFTER a response, set a valid response status code.
    [1000,{}, []]
  else
    [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
  end
end
Iodine.start
```

#### A few notes

This design has a number of benefits, some of them related to better IO handling, resource optimization (no need for two IO polling systems), etc. This also allows us to use middleware without interfering with connection upgrades and provides backwards compatibility.

Iodine::Rack imposes a few restrictions for performance and security reasons, such as that the headers (both sending and receiving) must be less than 8Kb in size. These restrictions shouldn't be an issue and are similar to limitations imposed by Apache.

Of course, if you still want to use Rack's `hijack` API, Iodine will support you - but be aware that you will need to implement your own reactor and thread pool for any sockets you hijack, as well as a socket buffer for non-blocking `write` operations (why do that when you can write a protocol object and have the main reactor manage the socket?).

### Performance oriented design - but safety first

Iodine is an evened server, similar in it's architecture to `nginx` and `puma`. It's different than the simple "thread-per-client" design that is often taught when we begin to learn about network programming.

By leveraging `epoll` (on Linux) and `kqueue` (on BSD), iodine can listen to multiple network events on multiple sockets using a single thread.

All these events go into a task queue, together with the application events and any user generated tasks, such as ones scheduled by [`Iodine.run`](http://www.rubydoc.info/github/boazsegev/iodine/Iodine#run-class_method).

In pseudo-code, this might look like this

```ruby
QUEUE = Queue.new

def server_cycle
    QUEUE << get_next_32_socket_events # these events schedule the proper user code to run
    QUEUE << [server]
end

def run_server
      while ((event = QUEUE.pop))
            event.shift.call(*event)
      end
end
```

In pure Ruby (without using C extensions or Java), it's possible to do the same by using `select`... and although `select` has some issues, it works well for smaller concurrency levels.

The server events are fairly fast and fragmented (longer code is fragmented across multiple events), so one thread is enough to run the server including it's static file service and everything... but single threaded mode should probably be avoided.

The thread pool is there to help slow user code. It's very common that the application's code will run slower and require external resources (i.e., databases, a pub/sub service, etc'). This slow code could "starve" the server (that is patiently waiting to run it's tasks on the same thread) - which is why a thread pool is often necessary.

The slower your application code, the more threads you will need to keep the server running smoothly.

### How does it compare to other servers?

Personally, after looking around, the only comparable servers are Puma and Passenger, which Iodine significantly outperformed on my tests (I didn't test Passenger's enterprise version).

Since the HTTP and Websocket parsers are written in C (with no RegExp), they're fairly fast.

Also, Iodine's core and parsers are running outside of Ruby's global lock, meaning that they enjoy true concurrency before entering the Ruby layer (your application) - this offers Iodine a big advantage over other Ruby servers.

Another assumption Iodine makes is that it is behind a load balancer / proxy (which is the normal way Ruby applications are deployed) - this allows Iodine to disregard header validity checks (we're not checking for invalid characters) and focus it's resources on other security and performance concerns.

I recommend benchmarking the performance for yourself using `wrk` or `ab`:

```bash
$ wrk -c200 -d4 -t12 http://localhost:3000/
# or
$ ab -n 100000 -c 200 -k http://127.0.0.1:3000/
```

Create a simple `config.ru` file with a hello world app:

```ruby
App = Proc.new do |env|
   [200,
     {   "Content-Type" => "text/html".freeze,
         "Content-Length" => "16".freeze },
     ['Hello from Rack!'.freeze]  ]
end

run App
```

Then start comparing servers. Here are the settings I used to compare Iodine and Puma (4 processes, 16 threads):

```bash
$ RACK_ENV=production iodine -p 3000 -t 16 -w 4
# vs.
$ RACK_ENV=production puma -p 3000 -t 16 -w 4
# Review the `iodine -?` help for more command line options.
```


When benchmarking with `wrk`, Iodine performed significantly better, (~62K req/sec vs. ~44K req/sec) while keeping a lower memory foot print (~60Mb vs. ~111Mb).

When benchmarking with `ab`, I got different results, where Iodine still performed significantly better, (~72K req/sec vs. ~36K req/sec and ~61Mb vs. ~81.6Mb). I suspect the difference between the two benchmarks has to do with system calls to `write`, but I have no real proof.

Remember to compare the memory footprint after running some requests - it's not just speed that C is helping with, it's also memory management and object pooling (i.e., Iodine uses a buffer packet pool management).

## Can I try before I buy?

Well, it is **free** and **open source**, no need to buy.. and of course you can try it out.

It's installable just like any other gem on MRI, run:

```
$ gem install iodine
```

If building the native C extension fails, please note that some Ruby installations, such as on Ubuntu, require that you separately install the development headers (`ruby.h` and friends). I have no idea why they do that, as you will need the development headers for any native gems you want to install - so hurry up and get them.

If you have the development headers but still can't compile the Iodine extension, [open an issue](https://github.com/boazsegev/iodine/issues) with any messages you're getting and I'll be happy to look into it.

## Mr. Sandman, write me a server

Girls love flowers, or so my ex used to keep telling me... but I think code is the way to really show that something is hot!

I mean, look at this short and sweet echo server - No HTTP, just use `telnet`... but it's so elegant I could cry:

```ruby

require 'iodine'

# an echo protocol with asynchronous notifications.
class EchoProtocol
  # `on_message` is an optional alternative to the `on_data` callback.
  # `on_message` has a 1Kb buffer that recycles itself for memory optimization.
  def on_message buffer
    # writing will never block and will use a buffer written in C when needed.
    write buffer
    # close will be performed only once all the data in the write buffer
    # was sent. use `force_close` to close early.
    close if buffer =~ /^bye[\r\n]/i
    # use buffer.dup to save the data from being recycled once we return.
    data = buffer.dup
    # run asynchronous tasks with ease
    run do
      sleep 1
      puts "Echoed data: #{data}"
    end
  end
end

# listen on port 3000 for the echo protocol.
Iodine.listen 3000, EchoProtocol
Iodine.threads = 1
Iodine.processes = 1
Iodine.start

```

## Why not EventMachine?

You can go ahead and use EventMachine if you like. They're doing amazing work on that one and it's been used a lot in Ruby-land... really, tons of good developers and people on that project, I'm sure...

But me, I prefer to make sure my development software runs the exact same code as my production software. So here we are.

Also, I don't really understand all the minute details of EventMachine's API, it kept crashing my system every time I reached 1K-2K active connections... I'm sure I just don't know how to use EventMachine, but that's just that.

Besides, you're here - why not take Iodine out for a spin and see for yourself?

## Can I contribute?

Yes, please, here are some thoughts:

* I'm really not good at writing automated tests and benchmarks, any help would be appreciated. I keep testing manually and that's less then ideal (and it's mistake prone).

* If we can write a Java wrapper for [the C libraries](https://github.com/boazsegev/facil.io), it would be nice... but it could be as big a project as the whole gem, as a lot of minor details are implemented within the bridge between these two languages.

* Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.

* If you love the project or thought the code was nice, maybe helped you in your own project, drop me a line. I'd love to know.

## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).

---

## "I'm also writing a Ruby extension in C"

Really?! That's great!

We could all use some more documentation around the subject and having an eco-system for extension tidbits would be nice.

Here's a few things you can use from this project and they seem to be handy to have (and easy to port):

* Iodine is using a [Registry](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/rb-registry.h) to keep dynamic Ruby objects that are owned by C-land from being collected by the garbage collector in Ruby-land...

    Some people use global Ruby arrays, adding and removing Ruby objects to the array, but that sounds like a performance hog to me.

    This one is a simple binary tree with a Ruby GC callback. Remember to initialize the Registry (`Registry.init(owner)`) so it's "owned" by some Ruby-land object, this allows it to bridge the two worlds for the GC's mark and sweep.

    I'm attaching it to one of Iodine's library classes, just in-case someone adopts my code and decides the registry should be owned by the global Object class.

* I was using a POSIX thread pool library ([`libasync.h`](https://github.com/boazsegev/facil.io/blob/master/lib/libasync.c)) until I realized how many issues Ruby has with non-Ruby threads... So now there's a Ruby-thread port for this library at ([`rb-libasync.h`](https://github.com/boazsegev/iodine/blob/master/ext/iodine/rb-libasync.h)).

    Notice that all the new threads are free from the GVL - this allows true concurrency... but, you can't make Ruby API calls in that state.

    To perform Ruby API calls you need to re-enter the global lock (GVL), albeit temporarily, using `rb_thread_call_with_gvl` and `rv_protect` (gotta watch out from Ruby `longjmp` exceptions).

* Since I needed to call Ruby methods while multi-threading and running outside the GVL, I wrote [`RubyCaller`](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/rb-call.h) which let's me call an object's method and wraps all the `rb_thread_call_with_gvl` and `rb_protect` details in a secret hidden place I never have to see again. It also keeps track of the thread's state, so if we're already within the GVL, we won't enter it "twice" (which will crash Ruby sporadically).

These are nice code snippets that can be easily used in other extensions. They're easy enough to write, I guess, but I already did the legwork, so enjoy.
