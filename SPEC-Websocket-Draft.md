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

* **Server**: When an upgrade request is received, the server will set the `env['upgrade.websockets?']` flag to `true`, indicating that: 1. this specific request is upgradable; and 2. this server supports specification.

* **Client**: When a client decides to upgrade a request, they will place a Websocket Callback Object (either a class or an instance) in the `env['upgrade.websockets']` Hash key.

* **Server**: The server will review the `env` Hash *before* sending the response. If the `env['upgrade.websockets']` was set, the server will perform the upgrade.

 * **Server**: The server will send the correct response status and headers, as will as any headers present in the response. The server will also perform any required housekeeping, such as closing the response body, if exists.

     The response status provided by the response object shall be ignored and the correct response status shall be set by the server.

* **Server**: Once the upgrade had completed, The server will add the required websocket/network functions to the callback handler or it's class (as aforementioned). If the callback handler is a Class object, the server will create a new instance of that class.

* **Server**: The server will call the `on_open` callback.

    No other callbacks shall be called until the `on_open` callback had returned.

    Websocket messages shall be handled by the `on_message` callback in the same order in which they arrive and the `on_message` will **not** be executed concurrently for the same connection.

    The `on_close` callback will **not** be called while `on_message` or `on_open` callbacks are running.

    The `on_ready` callback might be called concurrently with the `on_message` callback, allowing data to be sent even while other data is being processed. Multi-threading considerations may apply.
