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

     * Catch any issues (and Bugs) while in development - just ask AT&T about how important this is ;-)

Iodine is a C extension for Ruby, developed on Ruby MRI 2.3.0.

## Iodine 0.1.x - is this an upgrade?

This is **not** an upgrade, this is a **rewrite**.

Iodine 0.1.x was written in Ruby and had tons of bells and whistles and a somewhat different API. It was limited to 1024 concurrent connections.

Iodine 0.2.x is written in C, doesn't have as many bells and whistles (i.e., no Websocket Client) and has a stripped down API (simpler to learn). The connection limit is computed according to the system limits and connection overflows are terminated with an optional busy message, so the system won't crash.

## Mr. Sandman, write me a server

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

## Why not EventMachine?

You can go ahead and use EventMachine if you like. They're doing amazing work on that one and it's been used a lot in Ruby-land... really, tons of good developers and people on that project, I'm sure.

But me, I prefer to make sure my development software runs the exact same way as my production software, so here we are.

Also, I don't really understand all the minute details of EventMachine's API, it kept crashing my system every time I reached ~1024 active connections... I'm sure I just don't know how to use EventMachine, but that's just that.

Besides, you're here - why not take Iodine out for a spin and see for yourself?

## Do you want to contribute?

Yes, please, here are some thoughts:

* I'm really not good at writing automated tests and benchmarks, any help would be appreciated. I keep testing manually and it sucks (and it's mistake prone).

* Anybody knows how to document Ruby API written in C?

* Some of the code is still super raw and could be either optimized or improved upon.

* If we can write a Java wrapper for the C libraries, it would be nice... but it could be as big a project as the whole gem.

* Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.

* If you love the project or thought the code was nice, maybe helped you in your own project, drop me a line. I'd love to know.

## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).
