### Draft Inactivity Notice

This proposed draft is only implemented by Iodine and hadn't seen external activity in a while.

Even though a number of other development teams (such as the teams for the Puma and Passenger server) mentioned that they plan to implements this draft, Iodine seems to be the only server currently implementing this draft and it is unlikely that this initiative will grow to become a community convention.

I still believe it's important to separate the Websocket server from the Websocket API used by application developers and frameworks, much like Rack did for HTTP. I hope that in the future a community convention for this separation of concerns can be achieved.

---
## Rack Websockets

This is the proposed Websocket support extension for Rack servers.

Servers that publish Websocket support using the `env['upgrade.websocket?']` value are assume by users to follow the requirements set in this document and thus should follow the requirements set in herein.

This document reserves the Rack `env` Hash keys of `upgrade.websocket?` and `upgrade.websocket`.

## The Websocket Callback Object

Websocket connection upgrade and handling is performed using a Websocket Callback Object.

The Websocket Callback Object should be a class (or an instance of such class) who's instances implement any of the following callbacks:

* `on_open()` WILL be called once the upgrade had completed.

* `on_message(data)` WILL be called when incoming Websocket data is received. `data` will be a String with an encoding of UTF-8 for text messages and `binary` encoding for non-text messages (as specified by the Websocket Protocol).

    The *client* **MUST** assume that the `data` String will be a **recyclable buffer** and that it's content will be corrupted the moment the `on_message` callback returns.

* `on_ready()` **MAY** be called when the state of the out-going socket buffer changes from full to not full (data can be sent to the socket). **If** `has_pending?` returns `true`, the `on_ready` callback **MUST** be called once the buffer state changes.

* `on_shutdown()` MAY be called during the server's graceful shutdown process, _before_ the connection is closed and in addition to the `on_close` function (which is called _after_ the connection is closed.

* `on_close()` WILL be called _after_ the connection was closed for whatever reason (socket errors, parsing errors, timeouts, client disconnection, `close` being called, etc').

* `on_open`, `on_ready`, `on_shutdown` and `on_close` shouldn't expect any arguments (`arity == 0`).

The following method names are reserved for the network implementation: `write`, `close` and `has_pending?`.

The server **MUST** extend the Websocket Callback Object's *class* using `extend`, so that the Websocket Callback Object inherits the following methods:

* `write(data)` will attempt to send the data through the websocket connection. `data` **MUST** be a String. If `data` is UTF-8 encoded, the data will be sent as text. If `data` is binary encoded it will be sent as non-text (as specified by the Websocket Protocol).

    `write` has the same delivery promise as `Socket#write` (a successful `write` does **not** mean any of the data will reach the other side).

    `write` shall return `true` on success and `false` if the websocket is closed.

    A server **SHOULD** document whether `write` will block or return immediately. It is **RECOMMENDED** that servers implement buffered IO, allowing `write` to return immediately when resources allow and block (or, possibly, disconnect) when the IO buffer is full.

* `close` closes the connection once all the data in the outgoing queue was sent. If `close` is called while there is still data to be sent, `close` will only take effect once the data was sent.

    `close` shall always return `nil`.

* `has_pending?` queries the state of the server's buffer for the specific connection (i.e., if the server has any data it is waiting to send through the socket).

    `has_pending?`, shall return `true` **if** the server has data waiting to be written to the socket **and** the server promises to call the `on_ready` callback once the buffer is empty and the socket is writable. Otherwise (i.e., if the server doesn't support the `on_ready` callback), `has_pending?` shall return `false`.

    To clarify: **implementing `has_pending?` is semi-optional**, meaning that a server may choose to always return `false`, no matter the actual state of the socket's buffer.

The following keywords (both as method names and instance variable names) are reserved for the internal server implementation: `_server_ws` and `conn_id`.

* The `_server_ws` object is private and shouldn't be accessed by the client.

* The `conn_id` object may be used as a connection ID for any functionality not specified herein.

Connection `ping` / `pong`, timeouts and network considerations should be implemented by the server. It is **RECOMMENDED** (but not required) that the server send `ping`s to prevent connection timeouts and detect network failure.

Server settings **MAY** (not required) be provided to allow for customization and adaptation for different network environments or websocket extensions. It is **RECOMMENDED** that any settings be available as command line arguments and **not** incorporated into the application's logic.

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

---

## Iodine's Collection Extension to the Rack Websockets

The biggest benefit from Iodine's Collection Extension is that it allows the creation of pub/sub plugins and other similar extensions that require access to all the connected Websockets - no nee for the plugin to ask "where's the list" or "add this code to `on_open`" or anything at all, truly "plug and play".

This extension should be easy enough to implement using a loop or an array within each process context. These methods do **not** cross process boundaries and they are meant to help implement more advanced features (they aren't a feature as much as an "access point").

Internally, this extension also allows iodine to manage connection memory and resource allocation while relieving developers from rewriting the same workflow of connection storing (`on_open do ALL_WS << self; end `) and management. Knowing that the user doesn't keep Websocket object references, allows Iodine to safely optimize it's memory use.

* `Iodine::Websocket.each` (class method) will run a block of code for each connected Websocket Callback Object **belonging to the current process**. This can be called from outside of a Websocket Callback Object as well.

    ```ruby
    Iodine::Websocket.each {|ws| ws.write "You're connected to PID #{Process.pid}" }
    ```

* `Iodine::Websocket.defer(conn_id)` Schedules a block of code to run for the specified connection at a later time, (*if* the connection is open) while preventing concurrent code from running for the same connection object.

    ```ruby
    Iodine::Websocket.defer(self.conn_id) {|ws| ws.write "still open" }
    ```

* `#defer` (instance method) Schedules a block of code to run for the specified connection at a later time, (*if* the connection is still open) while preventing concurrent code from running for the same connection object.

    ```ruby
    defer { write "still open" }
    ```

* `Iodine::Websocket.count` (instance method) Returns the number of active websocket connections (including connections that are in the process of closing down) **belonging to the current process**.

    ```ruby
    write "#{Iodine::Websocket.count} clients sharing this process"
    ```

## Iodine's Pub/Sub Extension to the Rack Websockets

This extension separates the websocket Pub/Sub semantics from the Pub/Sub engine (i.e. Redis, MongoDB, etc') or the Server. If the Collection Extension is implemented, than this isn't necessarily a big step forward (the big step forward is what can be done with is).

I don't personally believe this has a chance of being adopted by other server implementors, but it's a very powerful tool that Iodine supports.

Including these semantics, in the form of the described API, allows servers and Pub/Sub engines to be optimized in different ways without distracting the application (or framework implementor with environmental details.

For example, Iodine includes two default Pub/Sub engines `Iodine::PubSub::Engine::CLUSTER` (the default) and `Iodine::PubSub::Engine::SINGLE_PROCESS` that implement a localized Pub/Sub engine within a process cluster.

 The Websocket module (i.e. `Iodine::Websocket`) includes the following singleton methods:

 * `Iodine::Websocket.default_pubsub=` sets the default Pub/Sub engine.

 * `Iodine::Websocket.default_pubsub` gets the default Pub/Sub engine.

Websocket Callback Objects inherit the following functions:

 * `#subscribe` (instance method) Subscribes to a channel using a specific "engine" (or the default engine). *Returns a subscription object (success) or `nil` (error)*. Doesn't promise actual subscription, only that the subscription request was scheduled to be sent.

    Messages from the subscription are sent directly to the client unless an optional block is provided.

    If an optional block is provided, it should accept the channel and message couplet (i.e. `{|channel, message| } `) and call the websocket `write` manually.

    All subscriptions (including server side subscriptions) MUST be automatically canceled **by the server** when the websocket closes.

 * `#subscription?` (instance method) locates an existing subscription, if any. *Returns a subscription object (success) or `nil` (error)*.

 * `#unsubscribe` (instance method) cancel / stop a subscription. Safely ignores `nil` subscription objects. Returns `nil`. Performance promise is similar to `subscribe` - the event was scheduled.

    Notice: there's no need to call this function during the `on_close` callback, since the server will cancel all subscriptions automatically.

 * `#publish` (instance method) publishes a message to engine's specified channel. Returns `true` on success or `false` on failure (i.e., engine error). Doesn't promise actual publishing, only that the message was scheduled to be published.

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
subscribe(pattern: "channel 5", force: text) do |channel, message|
    puts message
end
# It's impossible to locate an anonymous block subscriptions!
subscription? (pattern: "channel 5", force: text) do |channel, message|
    puts message
end
#  => nil # ! can't locate

```

Servers supporting this extension aren't required to implement any Pub/Sub engines, but they MUST implement a **bridge** between the server and Pub/Sub Engines that follows the following semantics:

Engines MUST inherit from a server specific Engine class and implement the following three methods that will be called by the server when required (servers might implement an internal distribution layer and call these functions at their discretion):

* `subscribe(channel, is_pattern)`: Subscribes to the channel. The `is_pattern` flag sets the type of subscription. Returns `true` / `false`.

* `unsubscribe(channel, is_pattern)`: Unsubscribes from the channel. The `is_pattern` flag sets the type of subscription. Returns `true` / `false`.

* `publish(channel, msg, is_pattern)`: Publishes to the channel (or all channels matching the pattern). Returns `true` / `false` (some engines, such as Redis, might not support pattern publishing, they should simply return `false`).

Engines inherit the following message from the server's Engine class and call it when messages were received:

* `distribute(channel, message, is_pattern = nil)`: If `channel` is a channel pattern rather than a channel name, the `is_pattern` should be set to `true` by the Engine. Engines that don't support pattern publishing, can simply ignore this flag.
