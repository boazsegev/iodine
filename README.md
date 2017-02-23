# Iodine - HTTP / Websocket Server & EventMachine alternative: C kqueue/epoll extension
[![Logo](https://github.com/boazsegev/iodine/raw/master/logo.png)](https://github.com/boazsegev/iodine)

[![Build Status](https://travis-ci.org/boazsegev/iodine.svg?branch=master)](https://travis-ci.org/boazsegev/iodine)
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

Iodine is a fast concurrent web server for real-time Ruby applications, with native support for Websockets, static file service and HTTP/1.1.

Iodine also supports custom protocol authoring, making Object Oriented **Network Services** easy to write.

Iodine is an **evented** framework with a simple API that builds off the low level [C code library facil.io](https://github.com/boazsegev/facil.io) with support for **epoll** and **kqueue** - this means that:

* Iodine can handle **thousands of concurrent connections** (tested with more then 20K connections).

    That's right, Iodine isn't subject to the 1024 connection limit imposed by native Ruby and `select`/`poll` based applications.

    This makes Iodine ideal for writing HTTP/2 and Websocket servers (which is what started this whole thing).

* Iodine supports only **Linux/Unix** based systems (i.e. OS X, Ubuntu, FreeBSD etc'), which are ideal for evented IO (while Windows and Solaris are better at IO *completion* events, which are totally different).

Iodine is a C extension for Ruby, developed for Ruby MRI 2.2.2 and up... it should support the whole Ruby 2.0 MRI family, but Rack requires Ruby 2.2.2, and so Iodine matches this requirement.

## Iodine::Rack - an HTTP and Websockets server

Iodine includes a light and fast HTTP and Websocket server written in C that was written according to the [Rack interface specifications](http://www.rubydoc.info/github/rack/rack/master/file/SPEC) and the Websocket draft extension.

### Running the web server

Using the Iodine server is easy, simply add Iodine as a gem to your Rack application:

```ruby
# notice that the `git` is required since Iodine 2.0 hadn't been released just yet.
gem 'iodine', :git => 'https://github.com/boazsegev/iodine.git'
```

Iodine will calculate, when possible, a good enough default concurrency model for fast applications... this might not fit your application if you use database access or other blocking calls.

To get the most out of Iodine, consider the amount of CPU cores available and the concurrency level the application requires.

Puma's model of 16 threads and 4 processes is easily adopted and proved to provide a good enough balance for most use-cases. Use:

```bash
bundler exec iodine -p $PORT -t 16 -w 4
```

It should be noted that automatic process scaling will cause issues with Websocket broadcast (`each`) support, ssince the `Websocket#each` method will be limited to the calling process (other clients might be connected to a different process.

I recommended that you consider using Redis to scale Websocket "events" across processes / machines. look into [plezi.io](http://www.plezi.io) for automatic Websocket scaling with Redis and Iodine.

### Writing data to the network layer

Iodine allows Ruby to write strings to the network layer. This includes HTTP and Websocket responses.

Iodine will handle an internal buffer (~4 to ~16 Mb, version depending) so that `write` can return immediately (non-blocking).

However, when the buffer is full, `write` will block until enough space in he buffer becomes available. Sending up to 16Kb of data (a single buffer "packet") is optimal. Sending a larger response might effect concurrency. Best Websocket response length is ~1Kb (1 TCP / IP packet) and allows for faster transmissions.

When using the Iodine's web server (`Iodine::Rack`), the static file service offered by Iodine streams files (instead of using the buffer). Every file response will require up to 2 buffer "packets" (~32Kb), one for the header and the other for file streaming.

This means that even a Gigabyte long response will use ~32Kb of memory, as long as it uses the static file service or the `X-Sendfile` extension (Iodine's static file service can be invoked by the Ruby application using the `X-Sendfile` header).

### Static file serving support

Iodine supports static file serving that allows the server to serve static files directly, with no Ruby layer (all from C-land).

This means that Iodine won't lock Ruby's GVL when sending static files. The files will be sent directly, allowing for true native concurrency. Since the Ruby layer is unaware of these requests, logging can be performed by turning iodine's logger on.

To setup native static file service, setup the public folder's address **before** starting the server.

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

### Special HTTP `Upgrade` support

Iodine's HTTP server includes special support for the Upgrade directive using Rack's `env` Hash, allowing the application to focus on services and data while Iodine takes care of the network layer.

Upgrading an HTTP connection can be performed either using Iodine's Websocket Protocol support with `env['upgrade.websocket']` or by implementing your own protocol directly over the TCP/IP layer - be it a websocket flavor or something completely different - using `env['upgrade.tcp']`.

#### Websockets

When an HTTP Upgrade request is received, Iodine will set the Rack Hash's upgrade property to `true`, so that: `env[upgrade.websocket?] == true`

To "upgrade" the HTTP request to the Websockets protocol, simply provide Iodine with a Websocket Callback Object instance or class: `env['upgrade.websocket'] = MyWebsocketClass` or `env['upgrade.websocket'] = MyWebsocketClass.new(args)`

Iodine will adopt the object, providing it with network functionality (methods such as `write`, `each`, `defer` and `close` will become available) and invoke it's callbacks on network events.

Here is a simple example we can run in the terminal (`irb`) or easily paste into a `config.ru` file:

```ruby
require 'iodine'
class WebsocketEcho
  def on_message data
    write data
  end
end
Iodine::Rack.app= Proc.new do |env|
  if env['upgrade.websocket?'.freeze] && env["HTTP_UPGRADE".freeze] =~ /websocket/i.freeze
    env['iodine.websocket'.freeze] = WebsocketEcho # or: WebsocketEcho.new
    [100,{}, []] # It's possible to set cookies for the response.
  else
    [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
  end
end
Iodine.start
```

#### TCP/IP (raw) sockets

Upgrading to a custom protocol (i.e., in order to implement your own Websocket protocol with special extensions) is performed almost the ame way, using `env['upgrade.tcp']`. In the following (terminal) example, we'll use an echo server without (direct socket echo):

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

This design has a number of benefits, some of them related to better IO handling, resource optimization (no need for two IO polling systems) etc'. This also allows us to use middleware without interfering with connection upgrades and provides up with backwards compatibility.

Iodine::Rack imposes a few restrictions for performance and security reasons, such as that the headers (both sending and receiving) must be less than 8Kb in size. These restrictions shouldn't be an issue and are similar to limitations imposed by Apache.

Here's a small HTTP and Websocket broadcast server with Iodine::Rack, which can be used directly from `irb`:

```ruby
require 'iodine'

# Our server controller and websockets handler
class My_Broadcast

  # handle HTTP requests (a class callback, emulating a Proc)
  def self.call env
    if env["HTTP_UPGRADE".freeze] =~ /websocket/i.freeze
      env['upgrade.websocket'.freeze] = self.new(env)
      [0,{}, []]
    end
    [200, {"Content-Length" => "12".freeze}, ["Hello World!".freeze]]
  end

  def initialize env
    @env = env # allows us to access the HTTP request data during the Websocket session
  end

  # handles websocket data (an instance  callback)
  def on_message data
    # data is the direct buffer and will be recycled once we leave this scope.
    # we'll copy it to prevent corruption when broadcasting the data asynchronously.
    data_copy = data.dup
    # We'll broadcast the data asynchronously to all open websocket connections.
    each {|ws| ws.write data_copy } # (limited to current process)
    close if data =~ /^bye[\r\n]/i
  end
end

# static file serving is as easy as (also supports simple byte serving):
Iodine::Rack.public = "www/public"

# start the server while setting the app at the same time
Iodine::Rack.run My_Broadcast
```

Of course, if you still want to use Rack's `hijack` API, Iodine will support you - but be aware that you will need to implement your own reactor and thread pool for any sockets you hijack, as well as a socket buffer for non-blocking `write` operations (why do that when you can write a protocol object and have the main reactor manage the socket?).

### How does it compare to other servers?

Personally, after looking around, the only comparable servers are Puma and Passenger (the open source version), which Iodine significantly outperformed on my tests.

Since the HTTP and Websocket parsers are written in C (with no RegExp), they're fairly fast.

Also, Iodine's core and parsers are running outside of Ruby's global lock, meaning that they enjoy true concurrency before entering the Ruby layer (your application) - this offers Iodine a big advantage over other Ruby servers.

Another assumption Iodine makes is that it is behind a load balancer / proxy (which is the normal way Ruby applications are deployed) - this allows Iodine to disregard header validity checks (we're not checking for invalid characters) which speeds up the parsing process even further.

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
```

Iodine performed almost twice as well, (~90K req/sec vs. ~44K req/sec) while keeping a memory foot print that was more then 20% lower (~65Mb vs. ~85Mb).

Review the `iodine -?` help for more data.

Remember to compare the memory footprint after running some requests - it's not just speed that C is helping with, it's also memory management and object pooling (i.e., Iodine uses a buffer packet pool management).

## Can I try before before I buy?

Well, it is **free** and **open source**, no need to buy.. and of course you can try it out.

It's installable just like any other gem on MRI, run:

```
$ gem install iodine
```

If building the native C extension fails, please notice that some Ruby installations, such as on Ubuntu, require that you separately install the development headers (`ruby.h` and friends). I have no idea why they do that, as you will need the development headers for any native gems you want to install - so hurry up and get it.

If you have the development headers but still can't compile the Iodine extension, [open an issue](https://github.com/boazsegev/iodine/issues) with any messages you're getting and I be happy to look into it.

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

## I loved Iodine 0.1.x - is this an upgrade?

This is **not** an upgrade, this is a **full rewrite**.

Iodine 0.1.x was written in Ruby and had tons of bells and whistles and a somewhat different API. It also inherited the `IO.select` limit of 1024 concurrent connections.

Iodine 0.2.x is written in C, doesn't have as many bells and whistles (i.e., no Websocket Client) and has a stripped down API (simpler to learn). The connection limit is calculated on startup, according to the system's limits. Connection overflows are terminated with an optional busy message, so the system won't crash.

## Why not EventMachine?

You can go ahead and use EventMachine if you like. They're doing amazing work on that one and it's been used a lot in Ruby-land... really, tons of good developers and people on that project, I'm sure...

But me, I prefer to make sure my development software runs the exact same code as my production software. So here we are.

Also, I don't really understand all the minute details of EventMachine's API, it kept crashing my system every time I reached ~1024 active connections... I'm sure I just don't know how to use EventMachine, but that's just that.

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
