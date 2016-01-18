# Iodine - a C kqueue/epoll EventMachine alternative
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

Iodine makes writing Object Oriented **Network Services** easy to write.

Iodine is an **evented** framework with a simple API that runs low level C code with support for **epoll** and **kqueue** - this means that:

* Iodine can handle thousands of concurrent connections (initially tested with ___ concurrent connections).

    That's right,  isn't subject to the 1024 connection limit imposed by native Ruby and `select`/`poll` based applications.

    This makes Iodine ideal for writing HTTP/2 and Websocket servers (which is it's intended design).

* Iodine runs **only on Linux/Unix** based systems (OS X, Ubunto, etc'). This is by design and allows us to:

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
    close if buffer.match /^bye[\r\n]/
    # use buffer.dup to save the data from being recycled once we return.
    data = buffer.dup
    # run asynchronous tasks with ease
    run {
      sleep 1
      puts "Echoed data: #{data}"
    }
  end
end

# create the server object and setup any settings we might need.
server = Iodine.new
server.threads = 1
server.processes = 4
server.busy_msg = "To many connections, try again later."
server.protocol = EchoProtocol
server.start

```

## Why not EventMachine?

Iodine started because I had an aversion to the EventMachine API.

Since I didn't understand all the minute details of EventMachine's API, it kept crashing my system every time I reached ~1024 active connections....

... I didn't like that.

Originally I wrote Iodine in Ruby. It had a nice API but suffered from the same connection limit. It didn't crash and it was way more fun to use, but in retrospect it was a worst.

Now, in version 0.2.0, Iodine is written in C.

Having said that, the people working on EventMachine did and keep doing an amazing job. Many people love EventMachine and it's a powerful tool. I personally didn't manage to develop a good relationship with the product, but reading some of the code I was amazed at the work that was put into it. Bravo.

## Do you want to contribute?

Yes, please, here are some thoughts:

* Some of the code is still super raw and could be either optimized or improved upon.

* If we can write a Java wrapper for the C libraries, it would be nice... but it could be as big a project as the whole gem.

* Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.

* If you love the project or thought the code was nice, maybe helped you in your own project, drop me a line. I'd love to know.

## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).
