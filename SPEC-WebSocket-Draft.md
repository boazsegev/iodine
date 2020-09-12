### Draft State

This draft is also implemented by [the Agoo server](https://github.com/ohler55/agoo) according to the specifications stated in [Rack PR#1272](https://github.com/rack/rack/pull/1272).

---
## Purpose

This document details a Rack specification extension for WebSocket / EventSource servers.

The purpose of this specification is:

1. To improve application safety by phasing out the use of `hijack` and replacing it with the use of application object callbacks.

    This should make it easer for applications to accept WebSocket and EventSource (SSE) connections without exposing themselves to risks and errors related to IO / network logic (such as slow client attacks, buffer flooding, etc').

2. To improve separation of concerns between servers and applications, moving the IO / network logic related to WebSocket and EventSource (SSE) back to the server.

    Simply put, when using a server that support this extension, the application / framework doesn’t need to have any knowledge about networking, transport protocols, IO streams, polling, etc'.

## Rack WebSockets / EventSource

Servers that publish WebSocket and/or EventSource (SSE) support using the `env['rack.upgrade?']` value **MUST** follow the requirements set in this document.

This document reserves the Rack `env` Hash keys of `rack.upgrade?` and `rack.upgrade`.

A conforming server **MUST** set `env['rack.upgrade?']` to `:websocket` for incoming WebSocket connections and `:sse` for incoming EventSource (SSE) connections. 

If a connection is not "upgradeable", a conforming server **SHOULD** set `env['rack.upgrade?']` to either `nil` or `false`. Setting the `env['rack.upgrade?']` to either `false` or `nil` should make it easier for applications to test for server support during a normal HTTP request.

If the connection is upgradeable and a client application set a value for `env['rack.upgrade']`:

* the server **MUST** use that value as a WebSocket / EventSource Callback Object unless the response status code `>= 300` (redirection / error status code).

* The response `body` **MUST NOT** be sent when switching to a Callback Object.

If a connection is **NOT** upgradeable and a client application set a value for `env['rack.upgrade']`:

* The server **SHOULD** ignore the Callback Object and process the response as if it did not exist.

* A server **MAY** use the Callback Object to allow a client to "hijack" the data stream as raw data stream. Such behavior **MUST** be documented.

### The WebSocket / EventSource Callback Object

WebSocket and EventSource connection upgrade and handling is performed using a Callback Object.

The Callback Object could be a any object which implements any (of none) of the following callbacks:

* `on_open(client)` **MUST** be called once the connection had been established and/or the Callback Object had been linked to the `client` object.

* `on_message(client, data)` **MUST** be called when incoming WebSocket data is received.

    This callback is ignored for EventSource connections.

    `data` **MUST** be a String with an encoding of UTF-8 for text messages and `binary` encoding for non-text messages (as specified by the WebSocket Protocol).

    The *callback object* **MUST** assume that the `data` String will be a **recyclable buffer** and that it's content will be corrupted the moment the `on_message` callback returns.

    Servers **MAY**, optionally, implement a **recyclable buffer** for the `on_message` callback. However, this is optional, is *not* required and might result in issues in cases where the client code is less than pristine.

* `on_drained(client)` **MAY** be called when the `client.write` buffer becomes empty. **If** `client.pending` ever returns a non-zero value (see later on), the `on_drained` callback **MUST** be called once the write buffer becomes empty.

* `on_shutdown(client)` **MAY** be called during the server's graceful shutdown process, _before_ the connection is closed and in addition to the `on_close` function (which is called _after_ the connection is closed.

* `on_close(client)` **MUST** be called _after_ the connection was closed for whatever reason (socket errors, parsing errors, timeouts, client disconnection, `client.close` being called, etc') or the Callback Object was replaced by another Callback Object.


The server **MUST** provide the Callback Object with a `client` object, that supports the following methods (this approach promises applications could be server agnostic):

* `env` **MUST** return the Rack `env` hash related to the originating HTTP request. Some changes to the `env` hash (such as removal of the IO hijacking support) **MAY** be implemented by the server.

* `write(data)` **MUST** schedule **all** the data to be sent. `data` **MUST** be a String. Servers **MAY** silently convert non-String objects to JSON if an application attempts to `write` a non-String value, otherwise servers **SHOULD** throw an exception.

    A call to `write` only promises that the data is scheduled to be sent. Implementation details may differ across servers.

    `write` shall return `true` on success and `false` if the connection is closed.

    For WebSocket connections only (irrelevant for EventSource connections):

    * If `data` is UTF-8 encoded, the data will be sent as text.

    * If `data` is binary encoded it will be sent as non-text (as specified by the WebSocket Protocol).

    A server **SHOULD** document its concurrency model, allowing developers to know whether `write` will block or not, whether buffered IO is implemented, etc'.

    For example, evented servers are encouraged to avoid blocking and return immediately, deferring the actual `write` operation for later. However, (process/thread/fiber) per-connection based servers **MAY** choose to return only after all the data was sent. Documenting these differences will allows applications to choose the model that best fits their needs and environments.

* `close` closes the connection once all the data scheduled using `write` was sent. If `close` is called while there is still data to be sent, `close` **SHOULD** return immediately and only take effect once the data was sent.

    `close` shall always return `nil`.

* `open?` **MUST** return `false` **if** the connection was never open, is known to be closed or marked to be closed. Otherwise `true` **MUST** be returned.

* `pending` **MUST** return -1 if the connection is closed. Otherwise, `pending` **SHOULD** return the number of pending writes (messages in the `write` queue\*) that need to be processed before the next time the `on_drained` callback is called.

    Servers **MAY** choose to always return the value `0` **ONLY IF** they never call the `on_drained` callback and the connection is open.

    Servers that return a positive number **MUST** call the `on_drained` callback when a call to `pending` would return the value `0`.

    \*Servers that divide large messages into a number of smaller messages (implement message fragmentation) **MAY** count each fragment separately, as if the fragmentation was performed by the user and `write` was called more than once per message.

* `pubsub?` **MUST** return `false` **unless** the pub/sub extension is supported.

   Pub/Sub patterns are idiomatic for WebSockets and EventSource connections but their API is out of scope for this extension.

* `class` **MUST** return the client's Class, allowing it be extended with additional features (such as Pub/Sub, etc').

    **Note**: Ruby adds this method automatically to every class, no need to do a thing.

The server **MAY** support the following (optional) methods for the `client` object:

* `handler` if implemented, **MUST** return the callback object linked to the `client` object.

* `handler=` if implemented, **MUST** set a new Callback Object for `client`.

    This allows applications to switch from one callback object to another (i.e., in case of credential upgrades).

    Once a new Callback Object was set, the server **MUST** call the old handler's `on_close` callback and **afterwards** call the new handler's `on_open` callback.

    It is **RECOMMENDED** (but not required) that this also updates the value for `env['rack.upgrade']`.

* `timeout` / `timeout=` allows applications to get / set connection timeouts dynamically and separately for each connection. Servers **SHOULD** provide a global setting for the default connection timeout. It is **RECOMMENDED** (but not required) that a global / default timeout setting be available from the command line (CLI).

* `protocol` if implemented, **MUST** return the same value that was originally set by `env['rack.upgrade?']`.


WebSocket `ping` / `pong`, timeouts and network considerations **SHOULD** be implemented by the server. It is **RECOMMENDED** (but not required) that the server send `ping`s to prevent connection timeouts and to detect network failure. Clients **SHOULD** also consider sending `ping`s to detect network errors (dropped connections).

Server settings **MAY** be provided to allow for customization and adaptation for different network environments or WebSocket extensions. It is **RECOMMENDED** that any settings be available as command line arguments and **not** incorporated into the application's logic.

---

## Implementation Examples

### Server-Client Upgrade to WebSockets / EventSource

* **Server**:

    When a regular HTTP request arrives (non-upgradeable), the server will set the `env['rack.upgrade?']` flag to `false`, indicating that: 1. this specific request is NOT upgradable; and 2. the server supports this specification for either WebSocket and/or EventSource connections.

    When a WebSocket upgrade request arrives, the server will set the `env['rack.upgrade?']` flag to `:websocket`, indicating that: 1. this specific request is upgradable; and 2. the server supports this specification for WebSocket connections.

    When an EventSource request arrives, the server will set the `env['rack.upgrade?']` flag to `:sse`, indicating that: 1. this specific request is an EventSource request; and 2. the server supports this specification for EventSource connections.

* **Client**:

    If a client decides to upgrade a request, they will place an appropriate Callback Object in the `env['rack.upgrade']` Hash key.

* **Server**:

    1. If the application's response status indicates an error or a redirection (status code `>= 300`), the server shall ignore the Callback Object and/or remove it from the `env` Hash, ignoring the rest of the steps that follow.

    2. The server will review the `env` Hash *before* sending the response. If the `env['rack.upgrade']` was set, the server will perform the upgrade.

    3. The server will send the correct response status and headers, as well as any headers present in the response object. The server will also perform any required housekeeping, such as closing the response body, if it exists.

         The response status provided by the response object shall be ignored and the correct response status shall be set by the server.

    4. Once the upgrade had completed, the server will call the `on_open` callback.

        No other callbacks shall be called until the `on_open` callback had returned.

        WebSocket messages shall be handled by the `on_message` callback in the same order in which they arrive and the `on_message` **SHOULD NOT** be executed concurrently for the same connection.

        The `on_close` callback **MUST NOT** be called while any other callback is running (`on_open`, `on_message`, `on_drained`, etc').

        The `on_drained` callback **MAY** be called concurrently with the `on_message` callback, allowing data to be sent even while incoming data is being processed. Multi-threading considerations apply.

## Example Usage

The following is an example WebSocket echo server implemented using this specification:

```ruby
module WSConnection
    def on_open(client)
        puts "WebSocket connection established (#{client.object_id})."
    end
    def on_message(client, data)
        client.write data # echo the data back
        puts "on_drained MUST be implemented if #{ pending } != 0."
    end
    def on_drained(client)
        puts "If this line prints out, on_drained is supported by the server."
    end
    def on_shutdown(client)
        client.write "The server is going away. Goodbye."
    end
    def on_close(client)
        puts "WebSocket connection closed (#{client.object_id})."
    end
    extend self
end

module App
   def self.call(env)
       if(env['rack.upgrade?'.freeze] == :websocket)
           env['rack.upgrade'.freeze] = WSConnection
           return [0, {}, []]
       end
       return [200, {"Content-Length" => "12", "Content-Type" => "text/plain"}, ["Hello World!"]]
   end
end

run App
```

The following example uses Push notifications for both WebSocket and SSE connections. The Pub/Sub API is subject to a separate Pub/Sub API extension and isn't part of this specification (it is, however, supported by iodine):

```ruby
module Chat
    def on_open(client)
        client.class.extend MyPubSubModule unless client.pubsub?
        client.subscribe "chat"
        client.publish "chat", "#{env[:nickname]} joined the chat."
    end
    def on_message(client, data)
        client.publish "chat", "#{env[:nickname]}: #{data}"
    end
    def on_close(client)
        client.publish "chat", "#{env[:nickname]}: left the chat."
    end
    extend self
end

module App
   def self.call(env)
       if(env['rack.upgrade?'.freeze])
           nickname = env['PATH_INFO'][1..-1]
           nickname = "Someone" if nickname == "".freeze
           env[:nickname] = nickname
           return [0, {}, []]
       end
       return [200, {"Content-Length" => "12", "Content-Type" => "text/plain"}, ["Hello World!"]]
   end
end

run App
```

Note that SSE connections will only be able to receive messages (the `on_message` callback is never called).
