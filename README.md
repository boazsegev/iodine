# Iodine
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)
[![GitHub](https://img.shields.io/badge/GitHub-Open%20Source-blue.svg)](https://github.com/boazsegev/iodine)

Iodine makes writing Object Oriented evented server applications easy to write.

In fact, it's so fun to write network protocols that mix and match together, that Iodine includes a built in Http, Http/2 (experimental) and Websocket server that act's a a great demonstration of the power behind Ruby and the Object Oriented approach.

To use Iodine, you just set up your tasks - including a single server, if you want one. Iodine will start running once your application is finished and it won't stop runing until all the scheduled tasks have completed.

Iodine is used by [the Plezi Ruby framework for real-time applications](http://www.plezi.io).

## Notice! and limitations...

Iodine 0.1.x is implemented in Ruby, using a `select` system call.

`select` is limited to 1024 open connections! after 1024 connection, `select` could cause "undefined behavior", including a possible "crash"... sometimes it's not important... some hosting environments only allow 1024 connections anyway... EventMachine crashes on my system after 1024 connections too...

...but websockets are connection hungry beasts.

Hence, a move to kqueue/epoll is required.

Iodine 0.2.x will include (hopefully), a kpoll / epoll implementation for Linux/BSD server environments, such as Heroku dynos (which run a flavor of unbuto 14).

However, it is unlikely that Iodine's beautiful API could survive this shift.

The 0.1.x API should be considered deprecated while a new API is under development (assuming I will manage to link some decent C code to Ruby)... I satrted out by displiking EventMachine's API, now it seems they might not have had a choice.


## Installation

Add this line to your application's Gemfile:

```ruby
gem 'iodine'
```

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install iodine

## Simple Usage: Running tasks and shutting down

This mode of operation is effective if you have a `cron`-job that periodically initiates an Iodine Ruby script. It allows the script to easily initiate a task's stack and perform the tasks concurrently.

Iodine starts to work once you app is finished setting all the tasks up (upon exit).

To see how that works, open your `irb` terminal an try this:


```ruby
require 'iodine'

# Iodine supports shutdown hooks
Iodine.on_shutdown { puts "Done!" }
# The last hook is the first scheduled for execution
Iodine.on_shutdown { puts "Finishing up :-)" }

# Setup tasks using the `run` or `callback` methods
Iodine.run do
    # tasks can create more tasks...
    Iodine.run { puts "Task 2 completed!" }
    puts "Task 1 completed!"
end

# set concurrency level (defaults to a single thread).
Iodine.threads = 5

# Iodine will start executing tasks once your script is done.
exit
```

In this mode, Iodine will continue running until all the tasks have completed and then it will quit. Timer-based tasks will be ignored.

## Simple Usage: Task polling

This mode of operation is effective if you want Iodine to periodically initiate new tasks such as when you are not able to use `cron`.

To initiate this mode, simply set: `Iodine.protocol = :timers` OR create a TimedEvent.

In example form:


```ruby
require 'iodine'

# set concurrency level (defaults to a single thread).
Iodine.threads = 5

# set Iodine to keep listening to TimedEvent(s).
Iodine.protocol = :timers

# perform a periodical task every ten seconds
Iodine.run_every 10 do
   Iodine.run { sleep 5; puts " * this could have been a long task..." }
   puts "I could be polling a database to schedule more tasks..."
end

# Iodine will start running once your script is done and it will never stop unless stopped.
exit
```

In this mode, Iodine will continue running until it receives a kill signal (i.e. `^C`). Once the kill signal had been received, Iodine will start shutting down, allowing up to ~20-25 seconds to complete any pending tasks (timeout).

## Server Usage: an Http and Websocket server

Using Iodine and leveraging Ruby's Object Oriented approach, is super fun to write our own network protocols and servers... This is Ioding itself includes an _optional_ Http and websocket server. Say "Hello World":

```ruby
# require the 'iodine/http' module if you want to use Iodine's Http server.
require 'iodine/http'
# returning a string will automatically append it to the response.
Iodine::Http.on_http { |request, response| "Hello World!" }
```

Iodine's Http server includes comes right out of the box with Websocket support as well as an experimental support for Http/2 (it's just a start, no `push` support just yet, but you can try it out).

Here's a quick chatroom server (use [www.websocket.org](http://www.websocket.org/echo.html) to check it out):

```ruby
# require the 'iodine/http' module if you want to use Iodine's Websocket server.
require 'iodine/http'
# create an object that will follow the Iodine Websocket API.
class WSChatServer < Iodine::Http::WebsocketHandler
  def on_open
     @nickname = request.params[:nickname] || "unknown"
     broadcast "#{@nickname} has joined the chat!"
     write "Welcome #{@nickname}, you have joined the chat!"
  end
  def on_message data
     broadcast "#{@nickname} >> #{data}"
     write ">> #{data}"
  end
  def on_broadcast data
     write data
  end
  def on_close
     broadcast "#{@nickname} has left the chat!"
  end
end

Iodine::Http.on_websocket WSChatServer
```

### Security and limits

Nobody wants their server to crash... Security measures are a fact of life as an internet entity. It is not only the theoretical malicious attacker from which a server must protect itself, but also from the unaware user or client.

Mostly, it is assumed that Iodine will run behind a proxy (i.e. within a Heroku Dyno or viaduct.io process), as such it is assumed that the proxy will protect the Iodine Http server from undue stress.

Having said that, Iodine is built with certain security measures in mind:

- Iodine will not accept IO data (neither from new connections nor form existing ones) while still answering existing requests and performing tasks. This safeguards against task overloading and DoS attacks causing a global crash, allowing the server to resume normal operation once a DoS attack had run it's course (and potentially allowing legitimate requests to be answered while the attack is still underway).

- Iodine will limit the query length, header count and header data size as well as well as react to header overloading by immediate disconnections. Iodine's limits are hardcoded to be slightly more than double those of common Proxies, so this counter-measure will only take effect should an attacker manage to bypass the Proxy.

- Iodine limits every Http request body-size (file upload data, form data, etc') to ~0.5GB. This setting can be changed using `Iodine::Http.max_body_size`. This safeguard is meant to prevent Ruby from crashing due to insufficient memory (an error Iodine cannot, and should not, recover from).

   It is recommended that this number will be lowered substantially whenever possible, by using `Iodine::Http.max_body_size = new_value`

   Do be aware that, at the moment, file upload data must passed through the memory on it's way to the temporary file. The parser's memory consumption will hopefully decrese in future releases, however, it is always recomended that large data be avoided when possible or handled using download/upload management protocols and services.

## Server Usage: Plug in your network protocol

Iodine is designed to help write network services (Servers) where each script is intended to implement a single server.

This is not a philosophy based on any idea or preferences, but rather a response to real-world design where each Ruby script is usually assigned a single port for network access (hence, a single server).

To help you write your network service, Iodine starts you off with the `Iodine::Protocol`. All network protocols should inherit from this class (or implement it's essencial functionality).

Here's a quick Echo server:

```ruby
require 'iodine'

# inherit from ::Iodine::Protocol
class EchoServer < Iodine::Protocol
    # The protocol class will call this withing a Mutex,
    # making sure the IO isn't accessed while being initialized.
	def on_open
		Iodine.info "Opened connection."
		set_timeout 5
	end
    # The protocol class will call this withing a Mutex, after reading the data from the IO.
    # This makes this thread-safe per connection.
	def on_message data
		write("-- Closing connection, goodbye.\n") && close if data =~ /^(bye|close|exit|stop)/i
		write(">> #{data.chomp}\n")
	end
	# Iodine makes sure this is called only once.
	def on_close
		Iodine.info "Closed connection."
	end
	# The is called whenever timeout is reached.
	# By default, ping will close the connection.
	# but we can do better...
	def ping
	    # `write` will automatically close the connection if it fails.
		write "-- Are you still there?\n"
	end
end


Iodine.protocol = EchoServer

# if running this code within irb:
exit
```

In this mode, Iodine will continue running until it receives a kill signal (i.e. `^C`). Once the kill signal had been received, Iodine will start shutting down, allowing up to ~20-25 seconds to complete any pending tasks (timeout).

## Server Usage: IP address & port, SSL/TLS and other command line options

Iodine automatically respects certain command line options that make it easier to use the same script over and over again with different results and making writing a `Procfile` (or similar setup files) a breeze.

Let `./script.rb` be an Iodine ruby script, may an easy one such as our Hello World:

```ruby
#!/usr/bin/env ruby

# script.rb
require 'iodine/http'

Iodine::Http.on_http do |request, response|
  response << "Hello World!"
end

```

Here are different command line options that Iodine recognizes automatically when running our script:

| purpose                                         | flag   | example                                  |
--------------------------------------------------|:------:|------------------------------------------|
|  Set the server's port.                         | `-p`   | `ruby ./script.rb -p 4000`               |
|  Limit the server's binding to a specific IP.   | `-ip`  | `ruby ./script.rb -p 4000 -ip 127.0.0.1` |
|  Use SSL/TLS on a specific port.                | `ssl`  | `ruby ./script.rb -p 3030 ssl`           |
|  Try out the experimental Http2 extention.      | `http2`|  `ruby ./script.rb -p 3030 ssl http2`    |

## Server Usage: Running more than one server

On some machines, Iodine will allow you to run more than a single server, by forking the main process while still running the script. This is more of a hack to be used in development environments, since runnig multiple instances of the script is the prefered way to use Iodine in production.

i.e.:

```ruby
require 'iodine/http'

# We'll use a simple hello world with a slight "tweek" for this example.
Iodine::Http.on_http do |request, response|
  response << "Hello World!"
  response << " We're on SSL/TLS!" if request.ssl?
end

Iodine.ssl = false

Process.fork do
   Iodine.ssl = true
   Iodine.port = 3030
   # # we can also change network behavior, so we could have used:
   # Iodine::Http.on_http { "Hello World! We're on SSL/TLS! - no `if` required ;-)" } 
end if Process.respond_to? :fork

# if using irb
exit
```

## Cuncurrency?

Iodine maintains the idea of: "yes" to concurrency between objects but "no" to concurrency within objects.

Iodine applies this concept when running in Task mode (or timer mode), by defaulting to single threaded mode, preventing multi-threading race conditions in an unknown environment. Also, each task runs in a single thread from the thread-pool, so unless it tries to set or manipulate global data, it's safe from race conditions.

Iodine applies this concept when running in Server mode by locking the Protocol instance whenever Iodine calls for actions related to that Protocol.

For instance, in Iodine's implementation for the Websocket protocol: Websocket messages to different connections can run concurrently, however multiple messages to the same connection are only executed one at a time, maintaining their order (lately a fix in version 0.1.8 made sure that also websocket broadcasting will be executed within the Protocol lock, preventing concurrency within the same connection).

The exception to the rule is the `ping` implementation. Your protocol's `ping` method will execute in parallel with other parts of your protocol's code. Pinging is a machanism that is often time sensitive and is required to maintain an open connection. For this reason, if your code is working hard on a long task, a ping will still occure automatically in the background and offset any connection timeout. 

If your code is short and efficient (no blocking tasks), it is best to run Iodine in a single threaded mode (you get better performance AND safer code, as long as you don't block):

      Iodine.threads = 1

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.


## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).

