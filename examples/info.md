# Ruby's Rack Push: Decoupling the real-time web application from the web

Something exciting is coming.

Everyone is talking about WebSockets and their older cousin EventSource / Server Sent Events (SSE). Faye and ActionCable are all the rage and real-time updates are becoming easier than ever.

But it's all a mess. It's hard to set up, it's hard to maintain. The performance is meh. In short, the existing design is expensive - it's expensive in developer hours and it's expensive in hardware costs.

However, [a new PR in the Rack repository](https://github.com/rack/rack/pull/1272) promises to change all that in the near future.

This PR is a huge step towards simplifying our code base, improving real-time performance and lowering the overall cost of real-time web applications. 

In a sentence, it's an important step towards [decoupling](https://softwareengineering.stackexchange.com/a/244478/224017) the web application from the web.

Remember, Rack is the interface Ruby frameworks (such and Rails and Sinatra) and web applications use to communicate with the Ruby application servers. It's everywhere. So this is a big deal.

## The Problem in a Nutshell

The problem with the current standard approach, in a nutshell, is that each real-time application process has to run two servers in order to support real-time functionality.

The two servers might be listening on the same port, they might be hidden away in some gem, but at the end of the day, two different IO event handling units have to run side by side.

"Why?" you might ask. Well, since you asked, I'll tell you (if you didn't ask, skip to the solution).

### The story of the temporary `hijack`

This is the story of a quick temporary solution coming up on it's 5th year as the only "standard" Rack solution available.

At some point in our history, the Rack specification needed a way to support long polling and other HTTP techniques. Specifically, Rails 4.0 needed something for their "live stream" feature.

For this purpose, [the Rack team came up with the `hijack` API approach](https://github.com/rack/rack/pull/481#issue-9702395).

This approach allowed for a quick fix to a pressing need. was meant to be temporary, something quick until Rack 2.0 was released (5 years later, Rack 2.0 is yet to be released).

The `hijack` API offers applications complete control of the socket. Just hijack the socket away from the server and voilá, instant long polling / SSE support... sort of.

That's where things started to get messy.

To handle the (now "free") socket, a lot of network logic had to be copied from the server layer to the application layer (buffering `write` calls, handling incoming data, protocol management, timeout handling, etc').

This is an obvious violation of the "**S**" in S.O.L.I.D (single responsibility), as it adds IO handling responsibilities to the application / framework.

It also violates the DRY principle, since the IO handling logic is now duplicated (once within the server and once within the application / framework).

Additionally, this approach has issues with HTTP/2 connections, since the network protocol and the application are now entangled.

### The obvious `hijack` price

The `hijack` approach has many costs, some hidden, some more obvious.

The most easily observed price is memory, performance and developer hours.

Due to code duplication and extra work, the memory consumption for `hijack` based solutions is higher and their performance is slower (more system calls, more context switches, etc').

Using `require 'faye'` will add WebSockets to your application, but it will take almost 9Mb just to load the gem (this is before any actual work was performed).

On the other hand, using the `agoo` or `iodine` HTTP servers will add both WebScokets and SSE to your application without any extra memory consumption.

To be more specific, using `iodine` will consume about 2Mb of memory, less than Puma, while providing both HTTP and real-time capabilities.

### The hidden `hijack` price

A more subtle price is higher hardware costs and a lower clients-per-machine ratio when using `hijack`.

Why?

Besides the degraded performance, the `hijack` approach allows some HTTP servers to lean on the `select` system call, (Puma used `select` last time I took a look).

This system call [breaks down at around the 1024 open file limit](http://man7.org/linux/man-pages/man2/select.2.html#BUGS), possibly limiting each process to 1024 open connections.

When a connection is hijacked, the sockets don't close as fast as the web server expects, eventually leading to breakage and possible crashes if the 1024 open file limit is exceeded.

## The Solution - Callbacks and Events 

[The new proposed Rack Push PR](https://github.com/rack/rack/pull/1272) offers a wonderful effective way to implement WebSockets and SSE while allowing an application to remain totally server agnostic.

This new proposal leaves the responsibility for the network / IO handling with the server, simplifying the application's code base and decoupling it from the network protocol.

By using a callback object, the application is notified of any events. Leaving the application free to focus on the data rather than the network layer.

The callback object doesn't even need to know anything about the server running the application or the underlying protocol.

The callback object is automatically linked to the correct API using Ruby's `extend` approach, allowing the application to remain totally server agnostic.

### How it works

Every Rack server uses a Hash type object to communicate with a Rack application.

This is how Rails is built, this is how Sinatra is built and this is how every Rack application / framework is built. It's in [the current Rack specification](https://github.com/rack/rack/blob/master/SPEC).

A simple Hello world using Rack would look like this (placed in a file called `config.ru`):

```ruby
# normal HTTP response
RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '12' }, [ 'Hello World!' ] ]
# note the `env` variable
APP = Proc.new {|env| RESPONSE }
# The Rack DSL used to run the application
run APP
```

This new proposal introduces the `env['rack.upgrade?']` variable.

Normally, this variable is set to `nil` (or missing from the `env` Hash).

However, for WebSocket connection, the `env['rack.upgrade?']` variable is set to `:websocket` and for EventSource (SSE) connections the variable is set to `:sse`.

To set a callback object, the `env['rack.upgrade']` is introduced (notice the missing question mark).

Now the design might look like this:

```ruby
# place in config.ru
RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '12' }, [ 'Hello World!' ] ]
# a Callback class
class MyCallbacks
  def on_open
    puts "* Push connection opened."
  end
  def on_message data
    puts "* Incoming data: #{data}"
  end
  def on_close
    puts "* Push connection closed."
  end
end
# note the `env` variable
APP = Proc.new do |env|
  if(env['rack.upgrade?'])
    env['rack.upgrade'] = MyCallbacks.new
    [0, {}, []]
  else
    RESPONSE
  end
end
# The Rack DSL used to run the application
run APP
```

Run this application with the Agoo or Iodine servers and let the magic sparkle.

For example, using Iodine:

```bash
# install iodine
gem install iodine
# start in single threaded mode
iodine -t 1
```

Now open the browser, visit [localhost:3000](http://localhost:3000) and open the browser console to test some Javascript.

First try an EventSource (SSE) connection (run in browser console):

```js
// An SSE example 
var source = new EventSource("/");
source.onmessage = function(msg) {
  console.log(msg.id);
  console.log(msg.data);
};
```

Sweet! nothing happened just yet (we aren't sending notifications), but we have an open SSE connection!

What about WebSockets (run in browser console):

```js
// A WebSocket example 
ws = new WebSocket("ws://localhost:3000/");
ws.onmessage = function(e) { console.log(e.data); };
ws.onclose = function(e) { console.log("closed"); };
ws.onopen = function(e) { e.target.send("Hi!"); };

```

Wow! Did you look at the Ruby console - we have working WebSockets, it's that easy.

And this same example will run perfectly using the Agoo server as well (both Agoo and Iodine already support the Rack Push proposal).

Try it:

```bash
# install the agoo server 
gem install agoo
# start it up
rackup -s agoo -p 3000
```

Notice, no gems, no extra code, no huge memory consumption, just the Ruby server and raw Rack (I didn't even use a framework just yet).

### The amazing push

So far, it's so simple, it's hard to notice how powerful this is.

Consider implementing a stock ticker, or in this case, a timer:

```ruby
# place in config.ru
RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '12' }, [ 'Hello World!' ] ]

# A live connection storage
module LiveList
  @list = []
  @lock = Mutex.new
  def self.<<(connection)
    @lock.synchronize { @list << connection }
  end
  def self.>>(connection)
    @lock.synchronize { @list.delete connection }
  end
  def self.broadcast(data)
    @lock.synchronize {
      @list.each {|c| c.write data }
    }
  end
end

# a Callback class
class MyCallbacks
  def on_open
    # add connection to the "live list"
    LiveList << self
  end
  def on_message(data)
    # Just an example broadcast
    LiveList.broadcast "Special Announcement: #{data}"
  end
  def on_close
    # remove connection to the "live list"
    LiveList >> self
  end
end

# Broadcast the time very second
Thread.new do
  while(true) do 
    sleep(1)
    LiveList.broadcast "The time is: #{Time.now}"
  end
end

# The Rack application
APP = Proc.new do |env|
  if(env['rack.upgrade?'])
    env['rack.upgrade'] = MyCallbacks.new
    [0, {}, []]
  else
    RESPONSE
  end
end
# The Rack DSL used to run the application
run APP
```

For this next example, I will use Iodine's pub/sub extension API to demonstrate the power offered by the new `env['rack.upgrade']` approach. This avoids the LiveList object and will make scaling easier.

Here is a simple chat room, but in this case I limit the interaction to WebSocket client. Why? because I can.

```ruby
# place in config.ru
RESPONSE = [200, { 'Content-Type' => 'text/html',
          'Content-Length' => '12' }, [ 'Hello World!' ] ]
# a Callback class
class MyCallbacks
  def initialize env
     @name = env["PATH_INFO"][1..-1]
     @name = "unknown" if(@name.length == 0)
  end
  def on_open
    subscribe :chat
    publish :chat, "#{@name} joined the chat."
  end
  def on_message data
    publish :chat, "#{@name}: #{data}"
  end
  def on_close
    publish :chat, "#{@name} left the chat."
  end
end
# note the `env` variable
APP = Proc.new do |env|
  if(env['rack.upgrade?'] == :websocket)
    env['rack.upgrade'] = MyCallbacks.new(env)
    [0, {}, []]
  else
    RESPONSE
  end
end
# The Rack DSL used to run the application
run APP
```

Now try (in the browser console):

```js
ws = new WebSocket("ws://localhost:3000/Mitchel");
ws.onmessage = function(e) { console.log(e.data); };
ws.onclose = function(e) { console.log("Closed"); };
ws.onopen = function(e) { e.target.send("Yo!"); };
```

### Why didn't anyone think of this sooner?

Actually, this isn't a completely new idea.

Evens as the `hijack` API itself was suggested, [an alternative approach was suggested](https://github.com/rack/rack/pull/481#issuecomment-11916942).

Another proposal was attempted [a few years ago](https://github.com/rack/rack/issues/1093).

But it seems things are finally going to change, as two high performance server, [agoo](https://github.com/ohler55/agoo) and [iodine](https://github.com/boazsegev/iodine) already support this new approach.

Things look promising.
