# Ruby Pub/Sub API Specification Draft

## Purpose

The pub/sub design is idiomatic to WebSocket and EventSource approaches as well as other reactive programming techniques.

The purpose of this specification is to offer a recommendation for pub/sub design that will allow applications to be implementation agnostic (not care which pub/sub extension is used)\*.

Simply put, applications will not have to worry about the chosen pub/sub implementation or about inter-process communication.

This should simplify the idiomatic `subscribe` / `publish` approach to real-time data pushing.

\* The pub/sub extension could be implemented by any external library as long as the API conforms to the specification. The extension will have to manage the fact that some servers `fork` and manage inter-process communication for pub/sub propagation (or limit it's support to specific servers). Also, servers that opt to implement the pub/sub layer, could perform optimizations related to connection handling and pub/sub lifetimes.

## Pub/Sub handling

Conforming Pub/Sub implementations **MUST** extend the WebSocket and SSE callback objects to implement the following pub/sub related methods (this requires that either the Pub/Sub implementation has knowledge about the Server OR that the Server has knowledge about the Pub/Sub implementation):

* `subscribe(to, opt = {}) { |from, message| optional_block }` where `opt` is a Hash object that supports the following possible keys (undefined keys *SHOULD* be ignored):

    * `:match` indicates a matching algorithm should be applied. Possible values should include [`:redis`](https://github.com/antirez/redis/blob/398b2084af067ae4d669e0ce5a63d3bc89c639d3/src/util.c#L46-L167), [`:nats`](https://nats.io/documentation/faq/#wildcards) or [`:rabbitmq`](https://www.rabbitmq.com/tutorials/tutorial-five-ruby.html). Pub/Sub implementations should support some or all of these common pattern resolution schemes.
    
    * `:handler` is an alternative to the optional block. It should accept Proc like opbjects (objects that answer to `call(from, msg)`).

    * If an optional `block` (or `:handler`) is provided, if will be called when a publication was received. Otherwise, the message alone (**not** the channel data) should be sent directly to the WebSocket / EventSource client.

    * `:as` accepts either `:text` or `:binary` Symbol objects.

        This option is only valid if the optional `block` is missing and the connection is a WebSocket connection. Note that SSE connections are limited to text data by design.

        This will dictate the encoding for outgoing WebSocket message when publications are directly sent to the client (as a text message or a binary blob). `:text` will be the default value for a missing `:as` option.
    
    If the `subscribe` method is called within a WebSocket / SSE Callback object, the subscription must be associated with the Callback object and closed automatically when the connection is closed.

    If the `subscribe` method isn't called from within a connection, it should be considered a global (non connection related) subscription and a `block` or `:handler` **MUST** be provided. 
    
    The `subscribe` method must return a subscription object if a subscription was scheduled (not necessarily performed). If it's already known that the subscription would fail, the method should return `nil`.

    The subscription object **MUST** support the method `close` (that will close the subscription).

    The subscription object **MAY** support the method `to_s` (that will return a String representing the stream / channel / pattern).

    The subscription object **MUST** support the method `==(str)` where `str` is a String object (that will return true if the subscription matches the String.

    A global variation for this method (allowing global subscriptions to be created) MUST be defined as `Rack::PubSub.subscribe`.

* `publish(to, message, engine = nil)` (preferably supporting named arguments) where:

    * `to` a String that identifies the channel / stream / subject for the publication ("channel" is the semantic used by Redis, it is similar to "subject" or "stream" in other pub/sub systems).

    * `message` a String with containing the data to be published.

    * `engine` (optional) routed the publish method to the specified Pub/Sub Engine (see later on). If none is specified, the default engine should be used.

    The `publish` method must return `true` if a publication was scheduled (not necessarily performed). If it's already known that the publication would fail, the method should return `false`.

    An implementation **MUST** call the relevant PubSubEngine's `publish` method after performing any internal book keeping logic. If `engine` is `nil`, the default PubSubEngine should be called. If `engine` is `false`, the implementation **MUST** forward the published message to the actual clients (if any).

    A global alias for this method (allowing it to be accessed from outside active connections) should be defined as `Rack::PubSub.publish`.

Implementations **MUST** implement the following methods:

* `Rack::PubSub.pubsub_register(engine)` where `engine` is a PubSubEngine object as described in this specification.

    When a pub/sub engine is registered, the implementation **MUST** inform the engine of any existing or future subscriptions.

    The implementation **MUST** call the engine's `subscribe` callback for each existing (and future) subscription.

* `Rack::PubSub.pubsub_default = engine` sets a default pub/sub engine, where `engine` is a PubSubEngine object as described in this specification.

    Implementations **MUST** forward any `publish` method calls to the default pub/sub engine.

* `Rack::PubSub.pubsub_default` returns the current default pub/sub engine, where the engine is a PubSubEngine object as described in this specification.

* `Rack::PubSub.pubsub_reset(engine)` where `engine` is a PubSubEngine object as described in this specification.

    Implementations **MUST** behave as if the engine was newly registered and (re)inform the engine of any existing subscriptions by calling engine's `subscribe` callback for each existing subscription.

Implementations **MAY** implement pub/sub internally (in which case the `pubsub_default` engine is the server itself or a server's module).

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

When a PubSubEngine object receives a published message, it should call:

```ruby
Rack::PubSub.publish channel: channel, message: message, engine: false
```
