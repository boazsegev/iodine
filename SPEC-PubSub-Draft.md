# Ruby pub/sub API Specification Draft

### Draft State

This draft is under discussion and will be implemented by iodine starting with the 0.8.x versions.

---

## Purpose

This document details a Rack specification extension for publish/subscribe (pub/sub) modules that can extend WebSocket / EventSource Rack servers.

The purpose of this specification is:

1. To keep separation of concerns by avoiding inter-process-communication logic (IPC) in the application code base.

   This is important since IPC is often implemented using pipes / sockets, which could introduce network and IO concerns into the application code.

2. To specify a common publish/subscribe (pub/sub) API for servers and pub/sub modules, allowing applications to easily switch between conforming implementations and servers.

    Since pub/sub design is idiomatic to WebSocket and EventSource approaches, as well as other reactive programming techniques, it is better if applications aren't locked in to a specific server / implementation.

3. To provide common considerations and guidelines for pub/sub implementors to consider when implementing their pub/sub modules.

    Some concerns are common for pub/sub implementors, such as integrating third party message brokers (Redis, RabbitMQ, Cassandra)

## Pub/Sub Instance Methods

Conforming pub/sub implementations **MUST** implement the following pub/sub instance methods:

* `pubsub?` **MUST** return the pub/sub API version as an integer number. Currently, set to `0` (development version).

* `subscribe(to, is_pattern = false) { |from, message| optional_block }` where:

    * `to` is a named **channel** (or **pattern**).

        The implementation **MAY** support pattern matching for named channels (`to`). The pattern matching algorithm used, if any, **SHOULD** be documented.

        If the implementation does **NOT** support pattern matching and `is_pattern` is truthful, the implementation **MUST** raise and exception.

        `to` **SHOULD** be a String, but implementations **MAY** support Symbols as aliases to Strings (in which case `:my_channel` is the same `'my_channel'`).

    * `block` is optional and accepts (if provided) two arguments (`from` which is equal to `to` and `message` which contains the data's content).

        `block` (if provided) **MUST** be called when a publication was received by the named channel.

        If no `block` is provided:

        * If the pub/sub instance is an extended WebSocket / EventSource (SSE) `client` object (see the WebSocket / EventSource extension draft) data should be directly sent to the `client`.

        * If the pub/sub isn't linked with a `client` / connection, an exception **MUST** be raised.

    If a subscription to `to` already exists for the same pub/sub instance, it should be *replaced* by the new subscription (the old subscription should be canceled / unsubscribed).

    If the pub/sub instance is an extended WebSocket / EventSource (SSE) `client` object (see the WebSocket / EventSource extension draft), the subscription **MUST** be closed automatically when the connection is closed (when `on_close` is called).

    Otherwise, the subscription **MUST** be closed automatically when the pub/sub object goes out of scope and is garbage collected (if this ever happens).
    
    The `subscribe` method **MUST** return `nil` on a known failure (i.e., when the connection is already closed), or any truthful value on success.

* `unsubscribe(from, is_pattern = false)` should cancel a subscription to the `from` named channel / pattern.

* `publish(to, message, engine = nil)` where:

    * `to` is a named channel, same as detailed in `subscribe`.

        Implementations **MAY** support pattern based publishing. This **SHOULD** be documented as well as how patterns are detected (as opposed to named channels). Note that pattern publishing isn't supported by common backends (such as Redis) and introduces complex privacy and security concerns.

    * `message` a String containing the data to be published.

        If `message` is NOT a String, the implementation **MAY** convert the data silently to JSON. Otherwise, the implementation **MUST** raise an exception.

        `message` encoding (binary / UTZ-8) **MAY** be altered during publication, but any change **MUST** result in valid encoding.

    * `engine` routes the publish method to the specified pub/sub Engine (see later on). If none is specified, the default engine should be used. If `false` is specified, the message **MUST** be forwarded to all subscribed clients on the **same process**.

    The `publish` method **MUST** return `true` if a publication was scheduled (not necessarily performed). If it's already known that the publication would fail, the method should return `false`.

    An implementation **MUST** call the relevant PubSubEngine's `publish` method after performing any internal book keeping logic. If `engine` is `nil`, the default PubSubEngine should be called. If `engine` is `false`, the implementation **MUST** forward the published message to the actual clients (if any).

    A global alias for this method (allowing it to be accessed from outside active connections) **MAY** be defined as `Rack::PubSub.publish`.

## Integrating a Pub/Sub module into a WebSocket / EventSource (SSE) `client` object

A conforming pub/sub module **MUST** be designed so that it can be integrated into WebSocket / EventSource (SSE) `client` objects be `include`-ing their class.

This **MUST** result in a behavior where subscriptions are destroyed / canceled once the `client` object state changes to "closed" - i.e., either when the `on_close` callback is called, or the first time the method `client.open?` returns `false`.

The idiomatic way to add a pub/sub module to a `client`'s class is:

```ruby
client.class.prepend MyPubSubModule unless client.pubsub?
```

The pub/sub module **MUST** expect and support this behavior.

**Note**: the use of `prepend` (rather than `include`) is chosen so that it's possible to override the `client`'s instance method of `pubsub?`.

## Connecting to External Backends (pub/sub Engines)

It is common for scaling applications to require an external message broker backend such as Redis, RabbitMQ, etc'. The following requirements set a common interface for such "engine" implementation and integration.

Pub/sub implementations **MUST** implement the following module / class methods in one of their public classes / modules (iodine implements these under `Iodine::PubSub`):

* `attach(engine)` where `engine` is a `PubSubEngine` object, as described in this specification.

    When a pub/sub engine is attached, the implementation **MUST** inform the engine of any existing or future subscriptions.

    The implementation **MUST** call the engine's `subscribe` callback for each existing (and future) subscription.

    The implementation **MUST** allow multiple "engines" to be attached when multiple calls to `attach` are made.

* `detach(engine)` where `engine` is a PubSubEngine object as described in this specification.

    The implementation **MUST** remove the engine from the attached engine list. The opposite of `attach`.

* `default=(engine)` sets a default pub/sub engine, where `engine` is a PubSubEngine object as described in this specification.

    Implementations **MUST** forward any `publish` method calls to the default pub/sub engine, unless an `engine` is specified in arguments passes to the `publish` method.

* `default` returns the current default pub/sub engine, where the engine is a PubSubEngine object as described in this specification.

* `reset(engine)` where `engine` is a PubSubEngine object as described in this specification.

    Implementations **MUST** behave as if the engine was newly registered and (re)inform the engine of any existing subscriptions by calling engine's `subscribe` callback for each existing subscription.

A `PubSubEngine` instance object **MUST** implement the following methods:

* `subscribe(to, is_pattern = false)` this method informs the engine that a subscription to the specified channel / pattern exists for the calling the process. It **MUST ONLY** be called **once** for all existing and future subscriptions to that channel within the process.

    The method **MUST** return `true` if a subscription was scheduled (or performed) or `false` if the subscription is known to fail.

    This method will be called by the pub/sub implementation (for each registered engine). The engine **MAY** assume that the method would never be called directly by an application / client.

    This method **MUST NOT** raise an exception.

* `unsubscribe(from, is_pattern = false)` this method informs an engine that there are no more subscriptions to the named channel / pattern for the calling process.

    The method's semantics are similar to `subscribe` only is performs the opposite action.

    This method **MUST NOT** raise an exception.

    This method will be called by the pub/sub implementation (for each registered engine). The engine **MAY** assume that the method would never be called directly by an application / client.

* `publish(channel, message)` where both `channel` is either a Symbol or a String (both being equivalent) and `message` **MUST** be a String.

    This method will be called by the server when a message is published using the engine.

    This method will be called by the pub/sub implementation (for each registered engine).

    The engine **MUST** assume that the method **MAY** be called directly by an application / client.

In order for a PubSubEngine instance object to publish messages to all subscribed clients on a particular process, it **SHOULD** call the implementation's global `publish` method with the engine set to `false`.

i.e., if the implementation's global `publish` method is in a class called `Iodine`:

```ruby
Iodine.publish channel, message, false
```
