# frozen_string_literal: true

# This is a WebSocket / SSE notification example application.
#
# In this example, WebSocket sub-protocols are explored.
#
# Running this application from the command line is easy with:
#
#      iodine
#
# Or, in a single thread and a single process:
#
#      iodine -t 1 -w 1
#
# Test using:
#
#      var subprotocol = "echo"; // or "chat"
#      ws = new WebSocket("ws://localhost:3000/Mitchel", subprotocol);
#      ws.onmessage = function(e) { console.log(e.data); };
#      ws.onclose = function(e) { console.log("Closed"); };
#      ws.onopen = function(e) { e.target.send("Yo!"); };


# Chat clients connect with the "chat" sub-protocol.
class ChatClient
  def on_open client
    @nickname = client.env['PATH_INFO'].to_s.split('/')[1] || "Guest"
    client.subscribe :chat
    client.publish :chat , "#{@nickname} joined the chat."
  end
  def on_close client
    client.publish :chat , "#{@nickname} left the chat."
  end
  def on_shutdown client
    client.write "Server is shutting down... disconnecting all clients. Goodbye."
  end
  def on_message client, message
    client.publish :chat , "#{@nickname}: #{message}"
  end
end

# Echo clients connect with the "echo" sub-protocol.
class EchoClient
  def on_open client
    client.write "You established an echo connection."
  end
  def on_shutdown client
    client.write "Server is shutting down... goodbye."
  end
  def on_message client, message
    client.write message
  end
end

# Rack application module
module APP
  # the allowed protocols
  CHAT_PROTOCOL_NAME = "chat"
  ECHO_PROTOCOL_NAME = "echo"
  PROTOCOLS =[CHAT_PROTOCOL_NAME, ECHO_PROTOCOL_NAME]

  # the Rack application
  def call env
    return [200, {}, ["Hello World"]] unless env["rack.upgrade?"]
    protocol = select_protocol(env)
    case(protocol)
    when CHAT_PROTOCOL_NAME
      env["rack.upgrade"] = ChatClient.new
      [101, { "Sec-Websocket-Protocol" => protocol }, []]
    when ECHO_PROTOCOL_NAME
      env["rack.upgrade"] = EchoClient.new
      [101, { "Sec-Websocket-Protocol" => protocol }, []]
    else
      [400, {}, ["Unsupported protocol specified"]]
    end
  end

  def select_protocol(env)
    request_protocols = env["HTTP_SEC_WEBSOCKET_PROTOCOL"]
    unless request_protocols.nil?
      request_protocols = request_protocols.split(/,\s?/) if request_protocols.is_a?(String)
      request_protocols.detect { |request_protocol| PROTOCOLS.include? request_protocol }
    end # either `nil` or the result of `request_protocols.detect` are returned
  end

  # make functions availble as singleton module
  extend self
end

run APP
