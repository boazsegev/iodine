# Iodine
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)

Iodine makes writing Object Oriented evented server applications easy to write.

In fact, it's so fun to write network protocols that mix and match together, that Iodine includes a built in Http, Http/2 (experimental) and Websocket server that act's a a great demonstration of the power behind Ruby and the Object Oriented approach.

To use Iodine, you just set up your tasks - including a single server, if you want one. Iodine will start running once your application is finished and it won't stop runing until all the scheduled tasks have completed.

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

In this mode, Iodine will continue running until all the tasks have completed and than it will quite. Timer based tasks will be ignored.

## Simple Usage: Task polling (unreleased version)

This mode of operation is effective if want Iodine to periodically initiates new tasks, for instance if you cannot use `cron`.

To initiate this mode, simply set: `Iodine.protocol = :timers`

In example:


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
Iodine::Http.on_http = { |request, response| "Hello World!" }
```

Iodine's Http server includes experimental support for Http/2 right out of the box as well as a Websocket server.

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

## Development

After checking out the repo, run `bin/setup` to install dependencies. Then, run `rake test` to run the tests. You can also run `bin/console` for an interactive prompt that will allow you to experiment.

To install this gem onto your local machine, run `bundle exec rake install`. To release a new version, update the version number in `version.rb`, and then run `bundle exec rake release`, which will create a git tag for the version, push git commits and tags, and push the `.gem` file to [rubygems.org](https://rubygems.org).

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/[USERNAME]/iodine.


## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).

