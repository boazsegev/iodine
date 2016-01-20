# Iodine - a C kqueue/epoll EventMachine alternative
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

Iodine makes writing Object Oriented **Network Services** easy to write.

Iodine is an **evented** framework with a simple API that runs low level C code with support for **epoll** and **kqueue** - this means that:

* Iodine can handle thousands of concurrent connections (tested with 20K).

    That's right,  isn't subject to the 1024 connection limit imposed by native Ruby and `select`/`poll` based applications.

    This makes Iodine ideal for writing HTTP/2 and Websocket servers (which is it's intended design).

* Iodine runs **only on Linux/Unix** based systems (OS X, Ubuntu, etc'). This is by design and allows us to:

     * Optimize our code for the production environment.

     * Have our testing and development machines behave the same as our ultimate production environment.

     * Catch any issues (read: bugs) while in development - just ask AT&T about how important this is ;-)

Iodine is a C extension for Ruby, developed with Ruby MRI 2.3.0 and 2.2.4 (it should support the whole Ruby 2.0 family, but it might not).

## Can I try before before I buy?

Well, it is **free** and **open source**, no need to buy.. and of course you can try it out.

It's installable like any other gem, just run:

```
$ gem install iodine
```

If building the native C extension fails, please notice that some Ruby installations, such as on Ubuntu, require that you separately install the development headers (`ruby.h` and friends). I have no idea why they do that, as you will need the development headers for any native gems you want to install - so hurry up and get it.

If you have the development headers but still can't compile the Iodine extension, [open an issue](https://github.com/boazsegev/iodine/issues) with any messages you're getting and I be happy to look into it.

## Mr. Sandman, write me a server

Girls love flowers, or so my ex used to keep telling me... but I think code is the way to really show that something is hot. I mean, look at this short and sweet echo server - it's so elegant I could cry:

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
    close if buffer.match /^bye[\r\n]/i
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

* Anybody knows how to document Ruby API written in C?

* Some of the code is still super raw and could be either optimized or improved upon.

* If we can write a Java wrapper for the C libraries, it would be nice... but it could be as big a project as the whole gem.

* Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.

* If you love the project or thought the code was nice, maybe helped you in your own project, drop me a line. I'd love to know.

## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).

---

## I'm also writing a Ruby extension in C

Really?! That's great!

We could all use some more documentation around the subject and having an eco-system for extension tidbits would be nice.

Here's a few things you can use from this project and they seem to be handy to have (and easy to port):

* Iodine is using a [Registry](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/rb-registry.h) to keep Ruby objects that are owned by C-land from being collected by the garbage collector...

    some people use global Ruby arrays, but that sounds like a performance hog to me.

    This one is a simple binary tree with a Ruby GC callback. Remember to initialize the Registry (`Registry.init(owner)`) so it's "owned" by some Roby-land object. I'm attaching it to one of Iodine's library classes, just in-case someone adopts my code and decides the registry should be owned by the global Object class.

* I was using a native thread pool library ([`libasync.h`](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/libasync.h)) until I realized how many issues Ruby has with POSIX threads... So now there's a Ruby-thread implementation for this library at ([`libasync-rb.c`](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/libasync-rb.c)).

    Notice that all the new threads are free from the GVL - this allows true concurrency... but, you can't make Ruby API calls in that state.

    To perform Ruby API calls you need to re-enter the global lock (GVL), albeit temporarily, using `rb_thread_call_with_gvl` and `rv_protect` (gotta watch out from Ruby `longjmp` exceptions).

* Since I needed to call Ruby methods while multi-threading and running outside the GVL, I wrote [`RubyCaller`](https://github.com/boazsegev/iodine/blob/0.2.0/ext/core/rb-call.h) which let's me call an object's method and wraps all the `rb_thread_call_with_gvl` and `rv_protect` details in a secret hidden place I never have to see again.

These are nice code snippets that can be easily used in other extensions. They're easy enough to write, I guess, but I already did the legwork, so enjoy.
