# Iodine limits and settings

I will, at some point, document these... here's a few things:

## HTTP/1.x limits

* HTTP Headers have a ~16Kb total size limit.

* Uploads are adjustable and limited to ~50Mib by default.

* HTTP keep-alive is adjustable and set to ~5 seconds by default.

## Websocket

* Incoming Websocket message size limits are adjustable and limited to ~250Kib by default.

## Pub/Sub

* Channel names are binary safe, but limited to 1024 characters (even though Redis doesn't seem to limit channel name length).

    Iodine uses a [trie](https://en.wikipedia.org/wiki/Trie) to match channel names. A trie offers binary exact matching with a fast lookup (unlike hash lookups, this offers zero name collision risk). However, it also means that channel names are memory **expensive**.

    As a side effect, sequencial channel names have an advantage over random channel names.

* Pub/sub is limited to the process cluster. To use pub/sub with an external service (such as Redis) an "Engine" is required (see YARD documentation).
