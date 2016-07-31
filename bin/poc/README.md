# A proof of concept for Rack's `env['rack.websocket']`

This is a proof of concept for Rack based Websocket connections, showing how the Rack API can be adjusted to support server native real-time connections.

The chosen proof of concept was the ugliest chatroom I could find.

Although my hope is that Rack will adopt the concept and make `env['rack.websocket?']` and `env['rack.websocket']` part of it's standard, at the moment it's an Iodine specific feature, implemented using `env['iodine.websocket']`.

## Install

Install required gems using:

```sh
bundler install
```

## Run

Run this application single threaded:

```sh
bundler exec iodine -- -www ./www
```

Or both multi-threaded and forked (you'll notice that memory barriers for forked processes prevent websocket broadcasting from reaching websockets connected to a different process).

```sh
bundler exec iodine -- -www ./www -t 16 -w 4
```

## Further reading

* https://github.com/rack/rack/issues/1093

* https://bowild.wordpress.com/2016/07/31/the-dark-side-of-the-rack

* https://github.com/boazsegev/iodine
