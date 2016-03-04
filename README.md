# Iodine - a C kqueue/epoll EventMachine alternative
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

Iodine 0.2.0 makes writing Object Oriented **Network Services** easy to write.

Iodine is an **evented** framework with a simple API that builds off a low level [C code library](https://github.com/boazsegev/c-server-tools) with support for **epoll** and **kqueue** - this means that:

* Iodine can handle **thousands of concurrent connections** (tested with 20K connections).

    That's right, Iodine isn't subject to the 1024 connection limit imposed by native Ruby and `select`/`poll` based applications.

    This makes Iodine ideal for writing HTTP/2 and Websocket servers (which is the reason for it's development).

* Iodine runs **only on Linux/Unix** based systems (i.e. OS X, Ubuntu, etc'). This allows us to:

     * Optimize our code for the production environment.

     * Have our testing and development machines behave the same as our ultimate production environment.

     * Catch any issues (read: bugs) while in development - just ask AT&T about how important this is ;-)

Iodine is a C extension for Ruby, developed with Ruby MRI 2.3.0 and 2.2.4 (it should support the whole Ruby 2.0 family, but it might not).

## Iodine::Rack - an HTTP and Websockets server

Iodine includes a light and fast HTTP and Websocket server written in C that was written according to the [Rack interface specifications](http://www.rubydoc.info/github/rack/rack/master/file/SPEC).

### Running the web server

Using the Iodine server is easy, simply add Iodine as a gem to your Rack application:

```ruby
# notice that the `git` is required since Iodine::Rack isn't yet officially released.
gem 'iodine', :git => 'https://github.com/boazsegev/iodine.git'
```

To get the most out of Iodine, consider the amount of CPU cores available and the concurrency level the application requires.

Puma's model of 16 threads and 4 processes is easily adopted and proved to provide a good enough balance for most use-cases. Use:

```bash
bundler exec iodine -p $PORT -t 16 - w 4
```

### Static file serving support

Iodine supports static file serving that allows the server to serve static files directly, with no Ruby layer (all from C-land).

This means that Iodine won't lock Ruby's GVL when sending static files (nor log these requests). The files will be sent directly, allowing for true native concurrency.

To setup native static file service, setup the public folder's address **before** starting the server. This is easily done by adding a single line to the application. i.e.:

```ruby
Iodine::Rack.public_folder = '/my/public/folder/'
```

### Special HTTP `Upgrade` support

Iodine's HTTP server includes special support for the Upgrade directive using Rack's `env` Hash to allow Hijacking as well as unique Protocol management that utilizes Iodine's reactor for better performance and integration.

To upgrade to the Websocket Protocol, utilizing Iodine's Websocket parser and protocol support, use `env['iodine.websocket'] = MyWebsocketClass`. i.e.:

```ruby
require 'iodine'
class WebsocketEcho
  def on_message data
    write data
  end
end
server = Iodine::Http.new
server.on_http= Proc.new do |env|
  if env["HTTP_UPGRADE".freeze] =~ /websocket/i.freeze
    env['iodine.websocket'.freeze] = WebsocketEcho # or: WebsocketEcho.new
    [0,{}, []] # It's possible to set cookies for the response.
  else
    [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
  end
end
```

Upgrading to a custom protocol (i.e., to implement your own Websocket protocol with proprietary extensions) is performed using `env['iodine.protocol']`. i.e., we'll use an echo server without Websockets (direct socket echo):

```ruby
require 'iodine'
class MyProtocol
  def on_message data
    # regular socket echo - NOT websockets.
    write data
  end
end
server = Iodine::Http.new
server.on_http= Proc.new do |env|
  if env["HTTP_UPGRADE".freeze] =~ /echo/i.freeze
    env['iodine.protocol'.freeze] = MyProtocol
    # no HTTP response will be sent when the status code is 0 (or less).
    # to upgrade AFTER a response, set a valid response status code.
    [0,{}, []]
  else
    [200, {"Content-Length" => "12"}, ["Welcome Home"] ]
  end
end
```

This is especially effective as it allows the use of middleware without interfering with connection upgrades.

This means that it's easy to minimize the number of Ruby objects we need before an Upgrade takes place and a new protocol is established.

Iodine::Rack imposes a few restrictions for performance and security reasons, such as that the headers (both sending and receiving) must be less then 8Kb in size. These restrictions shouldn't be an issue and are similar to limitations imposed by Apache.

Here's a small Http and Websocket broadcast server with Iodine::Rack, which can be used directly from `irb`:

```ruby
require 'iodine'

# Our server controller and websockets handler
class My_Broadcast

  # handle HTTP requests (a class callback, emulating a Proc)
  def self.call env
    if env["HTTP_UPGRADE".freeze] =~ /websocket/i.freeze
      env['iodine.websocket'.freeze] = self
      [0,{}, []]
    end
    [200, {"Content-Length" => "12"}, ["Hello World!"]]
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

# Iodine::Rack is a default HTTP server, instance designed for Rack applications
Iodine::Rack.on_http = My_Broadcast

# static file serving is as easy as (also supports simple byte serving):
Iodine::Rack.public_folder = "www/public/"

# start the server
Iodine::Rack.start
```

Of course, if you still want to use Rack's `hijack` API, Iodine will support you - but be aware that you will need to implement your own reactor and thread pool for any sockets you hijack (why do that when you can write a protocol object and have the main reactor manage the socket?).

### How does it compare to other servers?

Since the HTTP and Websocket parsers are written in C (with no RegExp), they're fairly fast.

Also, Iodine's core and parsers are running outside of Ruby's global lock, meaning that they enjoy true concurrency before entering the Ruby layer (your application) - this offers Iodine a big advantage over other servers.

I'm not posting any data because Iodine is still under development and things are somewhat dynamic - but you can compare the performance for yourself using `wrk` or `ab`:

```bash
$ wrk -c200 -d4 -t12 http://localhost:3000/
# or
$ ab -n 100000 -c 200 -k http://127.0.0.1:3000/
```

Create a simple `config.ru` file with a hello world app:

```ruby
App = Proc.new do |env|
   [200,
     {   "Content-Type".freeze => "text/html".freeze,
         "Content-Length".freeze => "16".freeze },
     ['Hello from Rack!'.freeze]  ]
end

run App
```

Then start comparing servers:

```bash
$ iodine -p 3000
```

vs.

```bash
$ rackup -p 3000 -E none -s <Other_Server_Here>
```

Puma ~16 threads by default, so when comparing against Puma, consider using an equal number of threads:

```bash
// (t - threads, w - worker processes)
$ iodine -p 3000 -t 16 -w 4
```

vs.

```bash
// (t - threads, w - worker processes)
$ puma -p 3000 -w 4 -q
```

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

I mean, look at this short and sweet echo server - it's so elegant I could cry:

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

# create the server object and setup any settings we might need.
server = Iodine.new
server.threads = 1
server.processes = 1
server.busy_msg = "To many connections, try again later."
server.protocol = EchoProtocol
server.start

```

## I loved Iodine 0.1.x - is this an upgrade?

This is **not** an upgrade, this is a **full rewrite**.

Iodine 0.1.x was written in Ruby and had tons of bells and whistles and a somewhat different API. It was limited to 1024 concurrent connections.

Iodine 0.2.x is written in C, doesn't have as many bells and whistles (i.e., no Websocket Client) and has a stripped down API (simpler to learn). The connection limit is calculated on startup, according to the system's limits. Connection overflows are terminated with an optional busy message, so the system won't crash.

## Why not EventMachine?

You can go ahead and use EventMachine if you like. They're doing amazing work on that one and it's been used a lot in Ruby-land... really, tons of good developers and people on that project, I'm sure.

But me, I prefer to make sure my development software runs the exact same code as my production software, so here we are.

Also, I don't really understand all the minute details of EventMachine's API, it kept crashing my system every time I reached ~1024 active connections... I'm sure I just don't know how to use EventMachine, but that's just that.

Besides, you're here - why not take Iodine out for a spin and see for yourself?

## Can I contribute?

Yes, please, here are some thoughts:

* I'm really not good at writing automated tests and benchmarks, any help would be appreciated. I keep testing manually and it sucks (and it's mistake prone).

* If we can write a Java wrapper for the C libraries, it would be nice... but it could be as big a project as the whole gem, as a lot of the implementation is written within the bridge between these two languages.

* Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.

* If you love the project or thought the code was nice, maybe helped you in your own project, drop me a line. I'd love to know.

## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).

---

## I'm also writing a Ruby extension in C

Really?! That's great!

We could all use some more documentation around the subject and having an eco-system for extension tidbits would be nice.

Here's a few things you can use from this project and they seem to be handy to have (and easy to port):

* Iodine is using a [Registry](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/rb-registry.h) to keep dynamic Ruby objects that are owned by C-land from being collected by the garbage collector in Ruby-land...

    Some people use global Ruby arrays, adding and removing Ruby objects to the array, but that sounds like a performance hog to me.

    This one is a simple binary tree with a Ruby GC callback. Remember to initialize the Registry (`Registry.init(owner)`) so it's "owned" by some Roby-land object, this allows it to bridge the two worlds for the GC's mark and sweep.

    I'm attaching it to one of Iodine's library classes, just in-case someone adopts my code and decides the registry should be owned by the global Object class.

* I was using a native thread pool library ([`libasync.h`](https://github.com/boazsegev/c-server-tools/blob/master/lib/libasync.c)) until I realized how many issues Ruby has with POSIX threads... So now there's a Ruby-thread implementation for this library at ([`rb-libasync.c`](https://github.com/boazsegev/iodine/blob/master/ext/iodine/rb-libasync.c)).

    Notice that all the new threads are free from the GVL - this allows true concurrency... but, you can't make Ruby API calls in that state.

    To perform Ruby API calls you need to re-enter the global lock (GVL), albeit temporarily, using `rb_thread_call_with_gvl` and `rv_protect` (gotta watch out from Ruby `longjmp` exceptions).

* Since I needed to call Ruby methods while multi-threading and running outside the GVL, I wrote [`RubyCaller`](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/rb-call.h) which let's me call an object's method and wraps all the `rb_thread_call_with_gvl` and `rb_protect` details in a secret hidden place I never have to see again.

These are nice code snippets that can be easily used in other extensions. They're easy enough to write, I guess, but I already did the legwork, so enjoy.
