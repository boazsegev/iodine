#! ruby

# A raw TCP/IP client example using iodine.
#
# The client will connect to a remote server and send a simple HTTP/1.1 GET request.
#
# Once some data was recieved, a delayed closure and shutdown signal will be sent to iodine.

# use a secure connection?
USE_TLS = true

# remote server details
$port = USE_TLS ? 443 : 80
$address = "google.com"


# require iodine
require 'iodine'

# Iodine runtime settings
Iodine.threads = 1
Iodine.workers = 1
Iodine.verbosity = 3 # warnings only


# a client callback handler
module Client

  def self.on_open(client)
    # Set a connection timeout
    client.timeout = 10
    # subscribe to the chat channel.
    puts "* Sending request..."
    client.write "GET / HTTP/1.1\r\nHost: #{$address}\r\n\r\n"
  end

  def self.on_message(client, data)
    # publish the data we received
    STDOUT.write data
    # close the client after a second... we're not really parsing anything, so it's a guess.
    Iodine.run_after(1000) { client.close }
  end

  def self.on_close(client)
    # stop iodine
    Iodine.stop
    puts "Done."
  end

  # returns the callback object (self).
  def self.call
    self
  end
end



if(USE_TLS)
  tls = Iodine::TLS.new
  tls.on_protocol("http/1.1") { Client }
end
# we use can both the `handler` keyword or a block, anything that answers #call.
Iodine.connect(address: $address, port: $port, handler: Client, tls: tls)

# start the iodine reactor
Iodine.start
