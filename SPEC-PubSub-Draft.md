# Common Pub/Sub API draft 

## Purpose

The pub/sub design is idiomatic to WebSocket and EventSource approaches.

The purpose of these specifications is to offer a recommendation for pub/sub design that will allow applications to be implementation agnostic (not care which pub/sub extension is used)\*.

Simply put, applications will not have to worry about the chosen pub/sub implementation or about inter-process communication.

This should simplify the idiomatic `subscribe` / `publish` approach to real-time data pushing.

\* The pub/sub extension could be implemented by an external library. The extension will have to manage the fact that some servers `fork` and manage inter-process communication for pub/sub propagation (or limit it's support to specific servers). Also, servers that opt to implement the pub/sub layer, could perform optimizations.

## Pub/Sub handling

Conforming Pub/Sub implementations **MUST** extend the WebSocket callback object to implement the following pub/sub related methods (this requires that either the Pub/Sub implementation has knowledge about the Server OR that the Server has knowledge about the Pub/Sub implementation):

* `subscribe(channel, opt = {}) { |channel, message| optional_block }` where `opt` is a Hash object that supports the following possible keys (undefined keys *SHOULD* be ignored):

    * `:pattern` indicates pattern matching should be applied. Possible values should include [`:redis`](https://github.com/antirez/redis/blob/398b2084af067ae4d669e0ce5a63d3bc89c639d3/src/util.c#L46-L167), [`:nats`](https://nats.io/documentation/faq/#wildcards) or [`:rabbitmq`](https://www.rabbitmq.com/tutorials/tutorial-five-ruby.html). Pub/Sub implementations should support some or all of these common pattern resolution schemes.
    
    * If an optional `block` is provided, the block will be called when a publication was received. Otherwise, the message alone (**not** the channel data) should be sent directly to the WebSocket / EventSource client.

    * `:as` accepts either `:text` or `:binary` Symbol objects.

        This option is only valid if the optional `block` is missing and the connection is a WebSocket connection.

        This will dictate the encoding for outgoing WebSocket message when publications are directly sent to the client (as a text message or a binary blob). `:text` will be the default value for a missing `:as`
    
    If the `subscribe` method is called within a WebSocket object, the subscription must be associated with the Callback object and closed automatically when the connection is closed.

    If the `subscribe` method isn't called from within a connection, it should be considered a global (non connection related) subscription and a block **MUST** be provided. 
    
    The `subscribe` method must return a subscription object if a subscription was scheduled (not necessarily performed). If it's already known that the subscription would fail, the method should return `nil`.

    The subscription object **MUST** support the method `close` (that will close the subscription).

    The subscription object **MUST** support the method `to_s` (that will return a String representing the stream / channel / pattern).

    The subscription object **MUST** support the method `==(str)` where `str` is a String object (that will return true if the subscription matches the String.

    A global variation for this method (allowing global subscriptions to be created) should be defined as `Rack::PubSub.subscribe`.

* `publish(args)` where `args` is a Hash object that supports (at least) the following possible keys:

    * `channel` a String that identifies the channel / stream / subject for the publication ("channel" is the semantic used by Redis and adopted herein, it is similar to "subject" or "stream" in other pub/sub systems).

    * `message` a String with similar semantics to  a Redis pattern subscription (performs glob matching to filter publications).

    * `engine` (optional) an with similar semantics to  a Redis pattern subscription (performs glob matching to filter publications).

    The `publish` method must return `true` if a publication was scheduled (not necessarily performed). If it's already known that the publication would fail, the method should return `false`.

    A server **SHOULD** call the relevant PubSubEngine's `publish` method after performing any internal book keeping logic. If `engine` is `nil`, the default PubSubEngine should be called. If `engine` is `false`, the server **MUST** forward the published message to the actual clients (if any).

    A global alias for this method (allowing it to be accessed from outside active connections) should be defined as `Rack::PubSub.publish`.

Servers **MUST** implement the following methods:

* `Rack::PubSub.pubsub_register(engine)` where `engine` is a PubSubEngine object as described in this specification.

    When a pub/sub engine is registered, the server **MUST** inform the engine of any existing or future subscriptions.

    The server **MUST** call the engine's `subscribe` callback for each existing (and future) subscription.

* `Rack::PubSub.pubsub_default = engine` sets a default pub/sub engine, where `engine` is a PubSubEngine object as described in this specification.

    Servers **MUST** forward any `publish` method calls to the default pub/sub engine.

* `Rack::PubSub.pubsub_default` returns the current default pub/sub engine, where the engine is a PubSubEngine object as described in this specification.

* `Rack::PubSub.pubsub_reset(engine)` where `engine` is a PubSubEngine object as described in this specification.

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
Rack::WebSocket.publish channel: channel, message: message, engine: false
```

---

## Iodine's General Extensions to the Rack WebSockets

Iodine adds the following WebSocket functions as extensions to the specification:

* `Iodine::WebSocket.defer(conn_id)` Schedules a block of code to run for the specified connection at a later time, (*if* the connection is open) while preventing concurrent code from running for the same connection object.

    ```ruby
    Iodine::WebSocket.defer(self.conn_id) {|ws| ws.write "still open" }
    ```

* `#defer` (instance method) Schedules a block of code to run for the specified connection at a later time, (*if* the connection is still open) while preventing concurrent code from running for the same connection object.

    ```ruby
    defer { write "still open" }
    ```

## Iodine's Pub/Sub Extension to the Rack WebSockets

This extension separates the WebSocket Pub/Sub semantics from the application, allowing Pub/Sub logic to be implemented in separate modules called Pub/Sub "engines" that could (potentially) be added to any conforming server.


For example, Iodine includes three Pub/Sub (native) engines `Iodine::PubSub::CLUSTER` (the default, for single machine multi-process pub/sub), `Iodine::PubSub::SINGLE_PROCESS` (a single process pub/sub) and `Iodine::PubSub::RedisEngine` (for horizontal scaling across machine boundaries, using Redis pub/sub)...

...but it would be easy enough to write a gem that will add another engine for MongoDB Pub/Sub and that engine would be server agnostic.

I don't personally believe this has a chance of being adopted by other server implementors, but it's a very powerful tool that Iodine supports.

The WebSocket module (i.e. `Iodine::WebSocket`) includes the following singleton methods:

 * `Iodine::WebSocket.default_pubsub=` sets the default Pub/Sub engine.

 * `Iodine::WebSocket.default_pubsub` gets the default Pub/Sub engine.

WebSocket Callback Objects inherit the following functions:

 * `#subscribe` (instance method) Subscribes to a channel. *Returns a subscription object (success) or `nil` (error)*. Doesn't promise actual subscription, only that the subscription request was reviewed and possibly *sent to all registered engines*.

    Messages from the subscription are sent directly to the client unless an optional block is provided.

    If an optional block is provided, it should accept the channel and message couplet (i.e. `{|channel, message| } `) and (optionally) call the websocket `write` manually.

    All subscriptions (including server side subscriptions) MUST be automatically canceled **by the server** when the websocket closes (the server wraps the engine's `subscribe` and manages subscription data in the WebSocket object).

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

# client side subscription forcing binary encoding for WebSocket protocol.
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
