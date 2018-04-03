### Draft Inactivity Notice

This proposed draft is only implemented by Iodine and hadn't seen external activity in a long while.

Even though a number of other development teams (such as the teams for the Puma and Passenger server) mentioned that they plan to implements this draft, Iodine seems to be the only server currently implementing this draft and it is unlikely that this initiative will grow to become a community convention.

I still believe it's important to separate the Websocket server from the Websocket API used by application developers and frameworks, much like Rack did for HTTP. I hope that in the future a community convention for this separation of concerns can be achieved.

---
## Rack Websockets

### Purpose

This is the proposed Websocket support extension for Rack servers.

The purpose of these specifications is:

1. To allow separation of concerns between the transport layer and the application, thereby allowing the application to be server agnostic.

    Simply put, when choosing between conforming servers, the application doesn’t need to have any knowledge about the chosen server.

2. To Support “native" (server-side) Websocket connections and Websocket events using application side callbacks.

    Simply put, to make it easy for applications to accept Websocket connections from Websocket clients (commonly browsers).

3. To Support “native” (server / extension side) pub/sub events\*.

    Simply put, applications will not have to worry about inter-process communication when handling pub/sub.

    This should simplify the idiomatic `subscribe` / `publish` approach to real-time data pushing.

    \* Although pub/sub isn't a server specific concern, there is high coupling between server concerns and pub/sub. Specifically: pub/sub events are often scheduled to be sent directly to the client; and applications shouldn't be concerned about (or require knowledge when) running within multi-process (forking) servers.


### Status

Servers that publish Websocket support using the `env['upgrade.websocket?']` value are assume by users to follow the requirements set in this document and thus should follow the requirements set in herein.

This document reserves the Rack `env` Hash keys of `upgrade.websocket?` and `upgrade.websocket`.

## The Websocket Callback Object

Websocket connection upgrade and handling is performed using a Websocket Callback Object.

The Websocket Callback Object should be a class (or an instance of such class) who's instances implement any of the following callbacks:

* `on_open()` WILL be called once the upgrade had completed.

* `on_message(data)` (REQUIRED) WILL be called when incoming Websocket data is received. `data` will be a String with an encoding of UTF-8 for text messages and `binary` encoding for non-text messages (as specified by the Websocket Protocol).

    The *client* **MUST** assume that the `data` String will be a **recyclable buffer** and that it's content will be corrupted the moment the `on_message` callback returns.

    Servers **MAY**, optionally, implement a **recyclable buffer** for the `on_message` callback. However, this is optional and it is *not* required.

* `on_ready()` **MAY** be called when the state of the out-going socket buffer changes from full to not full (data can be sent to the socket). **If** `has_pending?` returns `true`, the `on_ready` callback **MUST** be called once the buffer state changes.

* `on_shutdown()` **MAY** be called during the server's graceful shutdown process, _before_ the connection is closed and in addition to the `on_close` function (which is called _after_ the connection is closed.

* `on_close()` **MUST** be called _after_ the connection was closed for whatever reason (socket errors, parsing errors, timeouts, client disconnection, `close` being called, etc').

* `on_open`, `on_ready`, `on_shutdown` and `on_close` shouldn't expect any arguments (`arity == 0`).

The following method names are reserved for the network implementation: `write`, `close` and `has_pending?`.

The server **MUST** extend the Websocket Callback Object's *class* using `extend`, so that the Websocket Callback Object **inherits** the following methods:

* `write(data, text = true)` will attempt to send the data through the websocket connection. `data` **MUST** be a String. If `data` is UTF-8 encoded, the data will be sent as text. If `data` is binary encoded it will be sent as non-text (as specified by the Websocket Protocol).

    `write` has the same delivery promise as `Socket#write` (a successful `write` does **not** mean any of the data will reach the other side).

    `write` shall return `true` on success and `false` if the websocket is closed.

    A server **SHOULD** document whether `write` will block or return immediately. It is **RECOMMENDED** that servers implement buffered IO, allowing `write` to return immediately when resources allow and block (or, possibly, disconnect) when the IO buffer is full.

* `close` closes the connection once all the data in the outgoing queue was sent. If `close` is called while there is still data to be sent, `close` will only take effect once the data was sent.

    `close` shall always return `nil`.

* `open?` returns the state of the connection. Servers **SOULD** set the method to return `true` if the connection is open and `false` if the connection is closed or marked to be closed.

* `has_pending?` queries the state of the server's buffer for the specific connection (i.e., if the server has any data it is waiting to send through the socket).

    `has_pending?`, shall return `true` **if** the server has data waiting to be written to the socket **and** the server promises to call the `on_ready` callback once the buffer is empty and the socket is writable. Otherwise (i.e., if the server doesn't support the `on_ready` callback), `has_pending?` shall return `false`.

    To clarify: **implementing `has_pending?` is semi-optional**, meaning that a server may choose to always return `false`, no matter the actual state of the socket's buffer.

The following keywords (both as method names and instance variable names) are reserved for the internal server implementations: `_conn` and `conn_id`.

Connection `ping` / `pong`, timeouts and network considerations should be implemented by the server. It is **RECOMMENDED** (but not required) that the server send `ping`s to prevent connection timeouts and detect network failure.

Server settings **MAY** (not required) be provided to allow for customization and adaptation for different network environments or websocket extensions. It is **RECOMMENDED** that any settings be available as command line arguments and **not** incorporated into the application's logic.

The requirement that the server extends the class of the Websocket Callback Object (instead of the client application doing so explicitly) is designed to allow the client application to be more server agnostic.

## Upgrading

* **Server**: When an upgrade request is received, the server will set the `env['upgrade.websocket?']` flag to `true`, indicating that: 1. this specific request is upgradable; and 2. this server supports this specification.

* **Client**: When a client decides to upgrade a request, they will place a Websocket Callback Object (either a class or an instance) in the `env['upgrade.websocket']` Hash key.

* **Server**: The server will review the `env` Hash *before* sending the response. If the `env['upgrade.websocket']` was set, the server will perform the upgrade.

 * **Server**: The server will send the correct response status and headers, as well as any headers present in the response object. The server will also perform any required housekeeping, such as closing the response body, if exists.

     The response status provided by the response object shall be ignored and the correct response status shall be set by the server.

* **Server**: Once the upgrade had completed, The server will add the required websocket/network functions to the callback handler or it's class (as aforementioned). If the callback handler is a Class object, the server will create a new instance of that class.

* **Server**: The server will call the `on_open` callback.

    No other callbacks shall be called until the `on_open` callback had returned.

    Websocket messages shall be handled by the `on_message` callback in the same order in which they arrive and the `on_message` will **not** be executed concurrently for the same connection.

    The `on_close` callback will **not** be called while `on_message` or `on_open` callbacks are running.

    The `on_ready` callback might be called concurrently with the `on_message` callback, allowing data to be sent even while other data is being processed. Multi-threading considerations may apply.

## Pub/Sub handling

The pub/sub design is idiomatic to Websocket approaches and should be supported on any supported server.

However, servers **MAY** fail to publish or subscribe, thereby allowing servers to require external gems to implement these features (a server may opt to always fail).

Servers **MUST** extend the Websocket callback object to implement the following pub/sub related methods:

* `subscribe(args) { |channel, message| optional_block }` where `args` is a Hash object that supports the following possible keys (undefined keys *SHOULD* be ignored):

    * `:channel` a String with similar semantics to a Redis channel (requires an exact String match to receive publications).

    * `:pattern` a String with similar semantics to a Redis pattern subscription (performs glob matching to filter publications).
    
    * If an optional `block` is provided, the block will be called when a publication was received. Otherwise, the message (**not** the channel data) should be sent directly to the Websocket client.

    * `:as` accepts either `:text` or `:binary` Symbol objects. This will dictate the encoding for a WebSocket message that's directly sent to the client (as a text message or a binary blob). `:text` will be the default value for a missing `:as`
    
        This option is only valid if the optional `block` is missing.

    If the `subscribe` method is called within a Websocket object, the subscription must be associated with the Websocket object and closed automatically when the Websocket connection is closed.

    If the `subscribe` method isn't called from within a connection, it should be considered a global (non connection related) subscription and a block **MUST** be provided. 
    
    The `subscribe` method must return a subscription object if a subscription was scheduled (not necessarily performed). If it's already known that the subscription would fail, the method should return `nil`.

    The subscription object **MUST** support the method `close` (that will close the subscription).

    A global variation for this method (allowing global subscriptions to be created) should be defined as `Rack::Websocket.subscribe`.

* `publish(args)` where `args` is a Hash object that supports (at least) the following possible keys:

    * `channel` a String that identifies the channel / stream / subject for the publication ("channel" is the semantic used by Redis and adopted herein, it is similar to "subject" or "stream" in other pub/sub systems).

    * `message` a String with similar semantics to  a Redis pattern subscription (performs glob matching to filter publications).

    * `engine` (optional) an with similar semantics to  a Redis pattern subscription (performs glob matching to filter publications).

    The `publish` method must return `true` if a publication was scheduled (not necessarily performed). If it's already known that the publication would fail, the method should return `false`.

    A server **SHOULD** call the relevant PubSubEngine's `publish` method after performing any internal book keeping logic. If `engine` is `nil`, the default PubSubEngine should be called. If `engine` is `false`, the server **MUST** forward the published message to the actual clients (if any).

    A global alias for this method (allowing it to be accessed from outside active connections) should be defined as `Rack::Websocket.publish`.

Servers **MUST** implement the following methods:

* `Rack::Websocket.pubsub_register(engine)` where `engine` is a PubSubEngine object as described in this specification.

    When a pub/sub engine is registered, the server **MUST** inform the engine of any existing or future subscriptions.

    The server **MUST** call the engine's `subscribe` callback for each existing (and future) subscription.

* `Rack::Websocket.pubsub_default = engine` sets a default pub/sub engine, where `engine` is a PubSubEngine object as described in this specification.

    Servers **MUST** forward any `publish` method calls to the default pub/sub engine.

* `Rack::Websocket.pubsub_default` returns the current default pub/sub engine, where the engine is a PubSubEngine object as described in this specification.

* `Rack::Websocket.pubsub_reset(engine)` where `engine` is a PubSubEngine object as described in this specification.

    Servers **MUST** behave as if the engine was newly registered and (re)inform the engine of any existing subscriptions.

    The server **MUST** call the engine's `subscribe` callback for each existing (and future) subscription.

Servers **MAY** implement pub/sub internally (in which case the `pubsub_default` engine is the server itself or a server's module).

However, servers **MUST** support external pub/sub "engines" as described above, using PubSubEngine objects.

PubSubEngine objects **MUST** implement the following methods:

* `subscribe(channel, is_pattern)` this method performs the subscription to the specified channel.

    If `is_pattern` is `true`, the channel subscription should use glob matching rather than exact match (same semantics as the Redis PSUBSCRIBE command).

    The method must return `true` if a subscription was scheduled (or performed) or `false` if the subscription is known to fail.

    This method will be called by the server (for each registered engine). The engine may assume that the method would never be called directly by an application.

* `unsubscribe(channel, is_pattern)` this method performs closes the subscription to the specified channel.

    The method's semantics are similar to `subscribe`.

    This method will be called by the server (for each registered engine). The engine may assume that the method would never be called directly by an application.

* `publish(channel, message)` where both `channel` and `message` are String object.

    This method will be called by the server when a message is published using the engine.

    The engine **MUST** assume that the method might called directly by an application.

When a PubSubEngine object receives a published message, it must call:

```ruby
Rack::Websocket.publish channel: channel, message: message, engine: false
```

---

## Iodine's General Extensions to the Rack Websockets

Iodine adds the following Websocket functions as extensions to the specification:

* `Iodine::Websocket.defer(conn_id)` Schedules a block of code to run for the specified connection at a later time, (*if* the connection is open) while preventing concurrent code from running for the same connection object.

    ```ruby
    Iodine::Websocket.defer(self.conn_id) {|ws| ws.write "still open" }
    ```

* `#defer` (instance method) Schedules a block of code to run for the specified connection at a later time, (*if* the connection is still open) while preventing concurrent code from running for the same connection object.

    ```ruby
    defer { write "still open" }
    ```

## Iodine's Pub/Sub Extension to the Rack Websockets

This extension separates the websocket Pub/Sub semantics from the application, allowing Pub/Sub logic to be implemented in separate modules called Pub/Sub "engines" that could (potentially) be added to any conforming server.


For example, Iodine includes three Pub/Sub (native) engines `Iodine::PubSub::CLUSTER` (the default, for single machine multi-process pub/sub), `Iodine::PubSub::SINGLE_PROCESS` (a single process pub/sub) and `Iodine::PubSub::RedisEngine` (for horizontal scaling across machine boundaries, using Redis pub/sub)...

...but it would be easy enough to write a gem that will add another engine for MongoDB Pub/Sub and that engine would be server agnostic.

I don't personally believe this has a chance of being adopted by other server implementors, but it's a very powerful tool that Iodine supports.

The Websocket module (i.e. `Iodine::Websocket`) includes the following singleton methods:

 * `Iodine::Websocket.default_pubsub=` sets the default Pub/Sub engine.

 * `Iodine::Websocket.default_pubsub` gets the default Pub/Sub engine.

Websocket Callback Objects inherit the following functions:

 * `#subscribe` (instance method) Subscribes to a channel. *Returns a subscription object (success) or `nil` (error)*. Doesn't promise actual subscription, only that the subscription request was reviewed and possibly *sent to all registered engines*.

    Messages from the subscription are sent directly to the client unless an optional block is provided.

    If an optional block is provided, it should accept the channel and message couplet (i.e. `{|channel, message| } `) and (optionally) call the websocket `write` manually.

    All subscriptions (including server side subscriptions) MUST be automatically canceled **by the server** when the websocket closes (the server wraps the engine's `subscribe` and manages subscription data in the Websocket object).

 * `#subscription?` (instance method) locates an existing subscription, if any. *Returns a subscription object (success) or `nil` (error)*.

 * `#unsubscribe` (instance method) cancel / stop a subscription. Safely ignores `nil` subscription objects. *Returns `nil`*. Performance promise is similar to `subscribe` - the event was scheduled.

    Notice: there's no need to call this function during the `on_close` callback, since the server will cancel all subscriptions automatically.

 * `#publish` (instance method) publishes a message to a specific engine (or the default engine). Returns `true` on success or `false` on failure (i.e., engine error). Doesn't promise actual publishing, only that the message forwarded to the engine.

For example:

```ruby
# client side subscription
s = subscribe(channel: "channel 1") # => subscription ID?
# client side unsubscribe
unsubscribe(s)

# client side subscription forcing binary encoding for Websocket protocol.
subscribe(channel: "channel 1", encoding: :binary) # => subscription ID?

# client side pattern subscription without saving reference
subscribe(pattern: "channel [0-9]") # => subscription ID?
# client side unsubscribe
unsubscribe( subscription?(pattern: "channel [0-9]"))

# server side anonymous block subscription
s = subscribe(channel: "channel ✨") do |channel, message|
    puts message
end
# server side unsubscribe
unsubscribe(s)

# server side persistent block subscription without saving reference
block = proc {|channel, message| puts message }
subscribe({pattern: "*"}, &block)
# server side unsubscribe
unsubscribe subscription?({pattern: "*"}, &block)

# server side anonymous block subscription requires reference...
subscribe(pattern: "channel 5", encoding: :text) do |channel, message|
    puts message
end
# It's impossible to locate an anonymous block subscriptions!
subscription? (pattern: "channel 5", encoding: :text) do |channel, message|
    puts message
end
#  => nil # ! can't locate

```

To implement this extension:

- A server should manage a "registry" for channels, clients and pub/sub engines. Also, the server should notify all the "registered" pub/sub engines about the following two events:

    - A channel received it's first subscriber.
    - A channel lost it's last subscriber.

- A server should offer a way for engines to publish messages to registered clients (using the `#distribute` function provided).

    This can be achieved either using a process bound pub/sub option or a cluster bound option.


Engines MUST inherit from a server specific Engine class and implement the following three methods that will be called by the server when required (servers should implement an internal distribution layer and call these functions at their discretion):

* `subscribe(channel, is_pattern)`: Subscribes to the channel. The `is_pattern` flag sets the type of subscription. Returns `true` / `false`.

* `unsubscribe(channel, is_pattern)`: Unsubscribes from the channel. The `is_pattern` flag sets the type of subscription. Returns `true` / `false`.

* `publish(channel, msg)`: Publishes to the channel. Returns `true` / `false`. No promises made.

Engines inherit the following message from the server's Engine class and call it when messages were received:

* `distribute(channel, message)`:  Servers are free to implement this however they see fit. Return values should be ignored.
