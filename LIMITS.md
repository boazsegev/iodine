# Iodine limits and settings

I will, at some point, document these... here's the key points:

## HTTP limits

* Uploads are adjustable and limited to ~50Mib by default.

* HTTP connection timeout (keep-alive) is adjustable and set to ~60 seconds by default.

* HTTP total header size is adjustable and limited to ~32Kib by default.

* HTTP header line length size is limited to a hard-coded limit of 8Kb.

* HTTP headers count is limited to a hard-coded limit of 128 headers.

## Websocket

* Incoming Websocket message size limits are adjustable and limited to ~250Kib by default.

## EventSource / SSE

* Iodine will automatically attempt to send a `ping` event instead of disconnecting the connection. The ping interval is the same as the HTTP connection timeout interval.

## Pub/Sub

* Channel names are binary safe and unlimited in length. However, name lengths effect performance.

    **Do NOT allow clients to dictate the channel name**, as they might use extremely long names and cause resource starvation.

* Pub/sub is limited to the process cluster. To use pub/sub with an external service (such as Redis) an "Engine" is required (see YARD documentation).

* Pub/sub pattern matching supports only the Redis pattern matching approach. This makes patterns significantly more expensive and exact matches simpler and faster.

    It's recommended to prefer exact channel/stream name matching when possible.
