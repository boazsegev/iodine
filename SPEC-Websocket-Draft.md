### Draft State

This proposed draft is only implemented by Iodine and hadn't seen external activity in a long while.

Even though a number of other development teams (such as the teams for the Puma and Passenger server) mentioned that they plan to implements this draft, Iodine seems to be the only server currently implementing this draft at the moment.

I am also discussing a variation of this draft to be implemented by [the Agoo server](https://github.com/ohler55/agoo).

---
## Purpose

This document details WebSocket / EventSource connection support for Rack servers.

The purpose of these specifications is:

1. To allow separation of concerns between the transport layer and the application, thereby allowing the application to be server agnostic.

    Simply put, when choosing between conforming servers, the application doesn’t need to have any knowledge about the chosen server.

2. To Support “native" (server-side) WebSocket and EventSource (SSE) connections and using application side callbacks.

    Simply put, to make it easy for applications to accept WebSocket and EventSource (SSE) connections from WebSocket and EventSource clients (commonly browsers).

## Rack WebSockets / EventSource

Servers that publish WebSocket and/or EventSource (SSE) support using the `env['rack.upgrade?']` value MUST follow the requirements set in this document.

This document reserves the Rack `env` Hash keys of `rack.upgrade?` and `rack.upgrade`.

The historical use of `upgrade.websocket?` and `upgrade.websocket` (iodine 0.4.x) will be gradually deprecated.

### The WebSocket / EventSource Callback Object

WebSocket and EventSource connection upgrade and handling is performed using a Callback Object.

The Callback Object should be a class (or an instance of such class) where **instances** implement any of the following callbacks:

* `on_open()` WILL be called once the connection had been established.

* `on_message(data)` WILL be called when incoming WebSocket data is received.

    This callback is ignored for EventSource connections.

    `data` will be a String with an encoding of UTF-8 for text messages and `binary` encoding for non-text messages (as specified by the WebSocket Protocol).

    The *client* **MUST** assume that the `data` String will be a **recyclable buffer** and that it's content will be corrupted the moment the `on_message` callback returns.

    Servers **MAY**, optionally, implement a **recyclable buffer** for the `on_message` callback. However, this is optional and it is *not* required.

* `on_ready()` **MAY** be called when the state of the out-going socket buffer changes from full to not full (data can be sent to the socket). **If** `has_pending?` returns `true`, the `on_ready` callback **MUST** be called once the buffer state changes.

* `on_shutdown()` **MAY** be called during the server's graceful shutdown process, _before_ the connection is closed and in addition to the `on_close` function (which is called _after_ the connection is closed.

* `on_close()` **MUST** be called _after_ the connection was closed for whatever reason (socket errors, parsing errors, timeouts, client disconnection, `close` being called, etc').

* `on_open`, `on_ready`, `on_shutdown` and `on_close` shouldn't expect any arguments (`arity == 0`).

The following method names are reserved for the network implementation: `write`, `close`, `open?` and `has_pending?`.

The server **MUST** extend the Callback Object's *class* using `extend`, so the Callback Object **inherits** the following methods (this approach promises applications could be server agnostic\*):

* `write(data)` will attempt to send the data through the connection. `data` **MUST** be a String.

    `write` has the same delivery promise as `Socket#write` (a successful `write` does **not** mean any of the data will reach the other side).

    `write` shall return `true` on success and `false` if the connection is closed.

    For WebSocket connections only (irrelevant for EventSource connections):

    * If `data` is UTF-8 encoded, the data will be sent as text.

    * If `data` is binary encoded it will be sent as non-text (as specified by the WebSocket Protocol).

    A server **SHOULD** document whether `write` will block or return immediately. It is **RECOMMENDED** that servers implement buffered IO, allowing `write` to return immediately when resources allow and block (or, possibly, disconnect) when the IO buffer is full.

* `close` closes the connection once all the data in the outgoing queue was sent. If `close` is called while there is still data to be sent, `close` will only take effect once the data was sent.

    `close` shall always return `nil`.

* `open?` returns the state of the connection. Servers **SOULD** set the method to return `true` if the connection is open and `false` if the connection is closed or marked to be closed.

* `pending` SHOULD return the number of outgoing messages (calls to `write`) that need to be processed before the next time the `on_ready` callback is called.

    Servers MAY choose to always return the value `0` if they never call the `on_ready` callback.

    Servers that return a value other than zero MUST call the `on_ready` callback when a call to `pending` would return the value `0`.

    Servers that divide large messages into a number of smaller messages (implement message fragmentation) MAY count each fragment separately, as if the fragmentation was performed by the user and `write` was called more than once per message.

The following keyword(s) (both as method names and instance variable names) is reserved for the internal server implementations: `_sock`.

WebSocket `ping` / `pong`, timeouts and network considerations should be implemented by the server. It is **RECOMMENDED** (but not required) that the server send `ping`s to prevent connection timeouts and detect network failure.

Server settings **MAY** (not required) be provided to allow for customization and adaptation for different network environments or WebSocket extensions. It is **RECOMMENDED** that any settings be available as command line arguments and **not** incorporated into the application's logic.

\* The requirement that the server extends the class of the Callback Object (instead of the client application doing so explicitly) is designed to allow the client application to be server agnostic

To clarify, an implicit `extend` doesn't require a namespace, while an explicit `extend` does. By avoiding the requirement to explicitly extend the callback object, the application can be namespace agnostic.

### Upgrading to WebSockets / EventSource

* **Server**:

    When a WebSocket upgrade request arrives, the server will set the `env['rack.upgrade?']` flag to `:websocket`, indicating that: 1. this specific request is upgradable; and 2. the server supports this specification for WebSocket connections.

    When an EventSource request arrives, the server will set the `env['rack.upgrade?']` flag to `:sse`, indicating that: 1. this specific request is an EventSource request; and 2. the server supports this specification.

* **Client**:

    When a client decides to upgrade a request, they will place an appropriate Callback Object (either a class or an instance) in the `env['rack.upgrade']` Hash key.

* **Server**:

    1. The server will review the `env` Hash *before* sending the response. If the `env['rack.upgrade']` was set, the server will perform the upgrade.

        If the callback handler is a Class object, the server will create a new instance of that class.

    2. The server will send the correct response status and headers, as well as any headers present in the response object. The server will also perform any required housekeeping, such as closing the response body, if it exists.

         The response status provided by the response object shall be ignored and the correct response status shall be set by the server.

         If the application's response status indicates an error or a redirection (status code >= 300), the server shall ignore the Callback Object.

    3. Once the upgrade had completed and the server extended the Callback Object, it will call the `on_open` callback.

        No other callbacks shall be called until the `on_open` callback had returned.

        WebSocket messages shall be handled by the `on_message` callback in the same order in which they arrive and the `on_message` will **not** be executed concurrently for the same connection.

        The `on_close` callback will **not** be called while any other callback is running (`on_open`, `on_message`, `on_ready`, etc').

        The `on_ready` callback might be called concurrently with the `on_message` callback, allowing data to be sent even while other data is being processed. Multi-threading considerations may apply.

## Example Usage

The following is an example WebSocket echo server implemented using this specification:

```ruby
class WSConnection
    def on_open
        puts "WebSocket connection established."
    end
    def on_message(data)
        write data
        puts "on_ready MUST be implemented if #{ pending } != 0."
    end
    def on_ready
        puts "Yap,on_ready is implemented."
    end
    def on_shutdown
        write "The server is going away. Goodbye."
    end
    def on_close
        puts "WebSocket connection closed."
    end
end

module App
   def self.call(env)
       if(env['rack.upgrade?'.freeze] == :websocket)
           env['rack.upgrade'.freeze] = WSConnection.new
           return [0, {}, []]
       end
       return [200, {"Content-Length" => "12", "Content-Type" => "text/plain"}, ["Hello World!"]]
   end
end

run App
```

The following is uses Push notifications for both WebSocket and SSE connections. The Pub/Sub API isn't part of this specification:

```ruby
class Chat
    def initialize(nickname)
        @nickname = nickname
    end
    def on_open
        subscribe "chat"
        publish "chat", "#{@nickname} joined the chat."
    end
    def on_message(data)
        publish "chat", "#{@nickname}: #{data}"
    end
    def on_close
        publish "chat", "#{@nickname}: left the chat."
    end
end

module App
   def self.call(env)
       if(env['rack.upgrade?'.freeze])
           nickname = env['PATH_INFO'][1..-1]
           nickname = "Someone" if nickname == "".freeze
           env['rack.upgrade'.freeze] = Chat.new(nickname)
           return [0, {}, []]
       end
       return [200, {"Content-Length" => "12", "Content-Type" => "text/plain"}, ["Hello World!"]]
   end
end

run App
```

Note that SSE connections will only be able to receive messages (the `on_message` callback is never called).
