# Iodine
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)

Please notice that this change log contains changes for upcoming releases as well. Please refer to the current gem version to review the current release.

## Changes:

#### Change log v.0.7.57 (2023-09-04)

**Fix**: Fixes possible name collision when loading gem (`.rb` vs. `.so` loading). Credit to @noraj (Alexandre ZANNI) for opening issue #148. Credit to @janbiedermann (Jan Biedermann) for discovering the root cause and offering a solution.

#### Change log v.0.7.56 (2023-07-07)

**Support**: Adds teapot support (HTTP code 418). Credit to Aleksandar N. Kostadinov (@akostadinov) for issue #144 and PR #145.

#### Change log v.0.7.55 (2023-05-01)

**Fix**: Fixes `X-SENDFILE` support so it works will `POST` requests. Credit to @fgoepel for PR #141.

#### Change log v.0.7.54 (2023-03-15)

**Fix**: Fixes verbosity option in iodine's CLI where the value `0` was ignored. Credit to @jsaak for opening issue #139.

#### Change log v.0.7.53 (2023-01-13)

**Fix**: Fixes Unix socket permission limitations, so unix sockets are not limited to the owner of the process. Credit to Patrik Rak (@raxoft) for opening issue #136.

#### Change log v.0.7.52 (2022-12-10)

**Fix**: Fixes `Iodine.unsubscribe` which failed when a symbol was supplied. Credit to Patrik Rak (@raxoft) for opening issue #134.

**Fix**: Fixes `Iodine.is_worker` which gave an incorrect answer due to a copy & paste error. Credit to Patrik Rak (@raxoft) for opening PR #133.

#### Change log v.0.7.51 (2022-12-03)

**Fix**: Fixes CPU spin error caused by an uncaught integer overflow. Credit to Alexander Pavlenko (@AlexanderPavlenko) for opening issue #132.

#### Change log v.0.7.50 (2022-10-29)

**Fix**: Fixes some compatibility issues with Rack 3.0. Credit to @taku0 for opening issue #131.

**Fix**: Fixes compilation on Ruby 3.x - deprecation of `rc_cData`. Credit to Mohammad A. Ali (@oldmoe) for opening issue #128.

#### Change log v.0.7.49

**Fix**: Fixes an issue where named arguments for `subscribe` might not be properly recognized.

**Security**: Fixes an issue where a malformed WebSocket continuation frame might crash a worker process. Credit to @fabio for exposing this issue and opening issue #127.

#### Change log v.0.7.48 (2022-06-28)

**Fix**: Fixes an issue with `pong` WebSocket frames when a client sends a WebSocket `ping`. Credit to Lucas Kuan (@lucaskuan) for finding this issue and for PR #124.

#### Change log v.0.7.47 (2022-05-11)

**Fix**: Fixes an issue that causes `Rack::Lint` to complain about `SERVER_PORT` being an empty string. Credit to @adam12 (Adam Daniels) for PR #108 and issue #107.

#### Change log v.0.7.46 (2022-05-06)

**Fix**: Fixes the (erroneous) default insertion of the `last-modified` header in order to both better express the intent of RFC 2616 and prevent conflict with the `Rack::ETag` middleware. Credit to @raxoft (Patrik Rak) for opening issue #122.

#### Change log v.0.7.45 (2021-11-26)

**Security**: Fixes a number of issues with the HTTP parser that could have been leveraged in potential exploit attempts such as request smuggling. Credit to @dcepelik (David Čepelík).

**Compatibility**: This release adds experimental Windows support (I don't have Windows, nor Intel, I cannot test this). Credit for a lot of work by @janbiedermann (Jan Biedermann).

#### Change log v.0.7.44 (2021-02-28)

**Fix**: Fixes issue #103 where an empty String response would result in the word "null" being returned (no String object was created, which routed the NULL object to facil.io's JSON interpreter). Credit to @waghanza (Marwan Rabbâa) for exposing the issue.

**Fix**: Fixes a possible edge case race condition where the GC might free a channel name's String object before it's passed on to the user's callback.

**Fix**: Fixes a typo in the CLI documentation.

#### Change log v.0.7.43 (2020-11-03)

**Fix**: Fixes an issue where the GVL state in user-spawned threads is inaccurate. This issue only occurs if spawning a new thread and calling certain Iodine methods from a user thread.

**Fix**: validate that data passed by the user to `write` is a String object and print warnings / raise exceptions if t isn't. Credit to Vamsi Ambati for asking a question that exposed this issue.

#### Change log v.0.7.42 (2020-08-02)

**Fix**: Implement fix suggested by @Shelvak (Néstor Coppi) in issue #98.

#### Change log v.0.7.41 (2020-07-24)

**Fix**: Hot Restart failed because listening sockets were cleared away. Credit to Néstor Coppi (@Shelvak) for exposing issue #97.

**Fix**: CLI argument parsing is now only active when using the iodine CLI (or if defining `IODINE_PARSE_CLI` before requiring `iodine`). Credit to Aldis Berjoza (@graudeejs) for exposing issue #96.

#### Change log v.0.7.40 (2020-05-23)

**Fix**: fixed TLS logging and performance issues exposed by Franck Gille (@fgi) in issue #93.

#### Change log v.0.7.39 (2020-05-18)

**Security**: a request smuggling attack vector and Transfer Encoding attack vector in the HTTP/1.1 parser were exposed by Sam Sanoop from [the Snyk Security team (snyk.io)](https://snyk.io). The parser was updated to deal with these potential issues.

**Fix**: (`http`) fixes an issue with date calculation - exposed by Franck Gille (@fgi) in issue #92.

**Fix**: (`extconf`) fixes an installation concern raised by Benoit Daloze (@eregon), in issue #91. The default compiler is now used.

#### Change log v.0.7.38

**Fix**: (`http`) fixes an issue and improves support for chunked encoded payloads. Credit to Ian Ker-Seymer ( @ianks ) for exposing this, writing tests and opening both the issue #87 and the PR #88.

#### Change log v.0.7.37

**Fix**: requests will fail when the path contains a dangling `?` (empty query). Credit to @adam12 for exposing this and opening issue #86.

**Fix**: documentation improvements and corrections, credit to Caleb Albritton ( @WA9ACE ) and Don Morrison ( @elskwid ) for PRs #82 and #83. Credit to Nir ( @Roko131 ) and Aurel Branzeanu ( @texpert ) for opening and commenting on issue #84.

#### Change log v.0.7.36

**Fix**: avoids clobbering the namespace when attempting to set Rack's default handler. Credit to Caleb Albritton ( @WA9ACE ) for issue #80 and PR #81.

**Fix**: avoids some `SIGINT` signals during the shutdown procedure (rollback).

#### Change log v.0.7.35

**Fix**: fix memory leak in the x-sendfile path where Ruby objects no longer in use would still be protected by iodine.

**Update**: (`iodine`) add the `Iodine.running?` method. Credit to Ian Ker-Seymer (@ianks) for PR #78.

#### Change log v.0.7.34

**Security**: (`facil.io`, `http`) updated to facil.io 0.7.3, incorporating it's bug fixes and security updates.

#### Change log v.0.7.33

**Fix**: (`iodine`) exception protection would fail and crash if the exception throws wasn't of type `Exception`. I'm not sure how this would happen, but on some Ruby versions it appeared to have occur, maybe where a custom `raise` would be called with a non-exception type. The issue was fixed by testing for the availability of the `message` and `backtrace` functions. Credit to Jan Biedermann (@janbiedermann) for exposing this issue (#76).

**Fix**: (`cli`) the CLI interface no longer requires Rack to be installed. If Rack is installed, it will be used. Otherwise, a copy of the Rack::Builder code will execute, licensed under the MIT license from the Rack source code (Copyright (C) 2007-2019 [Leah Neukirchen](http://leahneukirchen.org/infopage.html)).

**Compatibility**: (`facil`) iodine would raise the signal `SIGINT` when shutting down in cluster mode, even if shutdown was initiated using `Iodine.stop`. Although this was designed to ensure worker processes would stop, this approach caused RSpec to stop testing and report an error. A temporary fix was applied and might be upstreamed to the facil.io repo. Credit to Jan Biedermann (@janbiedermann) for exposing this issue (#76).

#### Change log v.0.7.32

**Fix**: (`http1`) fixes a race-condition between the `on_ready` and `on_data` events, that could result in the `on_data` event being called twice instead of once (only possible with some clients). On multi-threaded workers, this could result in the CPU spinning while the task lock remains busy. Credit to Néstor Coppi (@Shelvak) for exposing the issue and providing an example application with detailed reports. Issue #75.

#### Change log v.0.7.31

**Security**: a heap-overflow vulnerability was fixed in the WebSocket parser. This attack could have been triggered remotely by a maliciously crafted message-header. Credit to Dane (4cad@silvertoque) for exposing this issue and providing a Python script demonstrating the attack. 

It's recommended that all iodine users update to the latest version.

#### Change log v.0.7.30

**Update**: (`cli`) added support for the `-pid` flag - stores the master processes PID in a file.

**Update**: (`cli`) added support for the `-config` (`-C`) flag - loads a configuration file immediately after loading iodine.

#### Change log v.0.7.29

**Fix**: fixed an issue where `env['rack.input'].read(nil, nil)` would return `nil` instead of `""` on zero-content requests (i.e., an empty POST request). Credit to @thexa4 (Max Maton) for exposing this issue and providing a POC for debugging (issue #71).

#### Change log v.0.7.28

**Fix**: fixed an issue where iodine would crush (or hang) if unprotected exceptions were raised within a response body's `each` loop. This also fixes Fiber support when streaming with Roda (note: iodine will concat the body in a buffer before sending it). Credit to @adam12 (Adam Daniels) both for exposing the issue (#70) and testing possible solutions.

#### Change log v.0.7.27

**Compatibility**: (`iodine`) fixed the HTTP request `SCRIPT_NAME` variable (in the Rack `env`) to default to the global environment variable `SCRIPT_NAME` when `SCRIPT_NAME` isn't root (`/`). Credit to @thexa4 (Max Maton) for exposing this compatibility concern (issue #68).

#### Change log v.0.7.26

**Fix**: (`http`) fixed HTTP date format to force the day of the month to use two digits. Credit to @ianks (Ian Ker-Seymer) for exposing this issue (issue #64).

**Fix**: (`iodine`) fixed static file service without an application (when using iodine as a stand-alone static file server).

**Fix**: (`fio`) miscellaneous compatibility updates.

#### Change log v.0.7.25

**Fix**: (`iodine`) fixed host name binding when running `iodine` using `rackup` or through `Rack`. Credit to @adam12 (Adam Daniels) for PR #60.

**Fix**: (`iodine`) removed bundler requirement in the `iodine.gemspec` file.

#### Change log v.0.7.24

**Fix**: (`fio`) fixed server shutdown on pub/sub stress, where internal pub/sub stress was mistakingly identified as a Slowloris attack. Credit to @moxgeek (Marouane Elmidaoui) for exposing this issue (plezi#32).

**Fix**: (`fio`): fixed Slowloris detection for buffer attack variation.

**Fix**: (`fio`): fixed `pending` result, where packet count wouldn't decrement until queue was drained.

**Updates**: (`fio`) facil.io updates, including pub/sub memory improvements for cluster mode.

#### Change log v.0.7.23

**Fix**: (`fio`): fixed logging message for overflowing log messages. Credit to @weskerfoot (Wesley Kerfoot) and @adam12 (Adam Daniels) for exposing the issue (issue #56).

**Updates**: (`fio`) facil.io updates.

#### Change log v.0.7.22

**Fix**: (`fio`, `redis`) fixed IPC messages between redis connections (in the master process) and callback blocks (executed in the worker processes). Credit to @moxgeek (Marouane Elmidaoui) for exposing this issue (plezi#31).

#### Change log v.0.7.21

**Fix**: (`iodine`, `redis`) Redis response was attempting to create Ruby objects outside the GIL. This is now fixed by entering the GIL earlier (before objects are created). Credit to @moxgeek (Marouane Elmidaoui) for exposing this issue (plezi#31).

**Fix**: (`redis`) fixed Redis reconnection. Address and port data was mistakingly written at the wrong address, causing it to be overwritten by incoming (non-pub/sub) data.

**Fix**: (`redis`) fixed a race condition in the Redis reconnection logic which might have caused more then a single pub/sub connection to be established and the first pending command to be sent again.

#### Change log v.0.7.20

**Security**: (`fio`) lower and smarter Slowloris detection limits (backlog limit is now 1,024 responses / messages per client). 

**Security**: (`http`) HTTP/1.1 slow client throttling - new requests will not be consumed until pending responses were sent. Since HTTP/1.1 is a response-request protocol, this protocol specific approach should protect the HTTP application against slow clients.

**Fix**: (`iodine`) remove redundant Content-Type printout. Credit to @giovannibonetti (Giovanni Bonetti) for exposing the issue (#53).

**Fix**: (`fio`) fix capacity maximization log to accommodate issues where `getrlimit` would return a `rlim_max` that's too high for `rlim_cur` (macOS).

**Fix**: (`fio`) fix uninitialized `kqueue` message in `fio_poll_remove_fd`.

**Fix**: (`docs`) @giovannibonetti (Giovanni Bonetti) fixed an error in the Rails README section, PR #52.

#### Change log v.0.7.19

**Deprecation**: (`iodine`) deprecated the CLI option `-tls-password`, use `-tls-pass` instead.

**Security**: (`fio`) Slowloris mitigation is now part of the core library, where `FIO_SLOWLORIS_LIMIT` pending calls to `write` (currently 4,096 backlogged calls) will flag the connection as an attacker and close the connection. This protocol independent approach improves security.

**Fix**: (`iodine`) log open file / socket limit per worker on startup.

**Fix**: (`iodine`) application warm-up error was fixed. Deprecation warnings will still print for deprecated symbols loaded due to the warm-up sequence.

**Update**: (`iodine`, `cli`) Support the environment variables `"WORKERS"` and `"THREADS"` out of the box (jury is out regarding `"PORT"` and `"ADDRESS"`, just use CLI for now).

#### Change log v.0.7.18

**Fix** (`pubsub`) fixed pub/sub for longer WebSocket messages. Issue where network byte ordering wasn't always respected and integer bit size was wrong for larger payloads. Credit to Marouane Elmidaoui (@moxgeek) for exposing the issue.

#### Change log v.0.7.17

**Security**: (`fio`) improved security against hash flooding attacks.

**Update**: (`iodine`) SSL/TLS support!

**Update**: (`iodine`) WebSocket client connections are now supported using `Iodine.connect` (both `ws://` and `wss://`)!

**Deprecation**: (`iodine`) deprecated `DEFAULT_HTTP_ARGS` in favor of `DEFAULT_SETTEINGS`.

**Deprecation**: (`iodine`) deprecated `Iodine.listen2http` in favor of `Iodine.listen service: :http`.

**Fix**: (`iodine` / `pubsub`) fixed possible issue with global subscriptions (non-connection bound subscriptions).

**Fix**: (`Iodine::Mustache`) fixed support for named argument, documentation and loading template from memory (rather than file) when creating a new `Iodine::Mustache` object.

**Fix**: (`redis`) fixed an issue where destroying the Redis engine and exiting pre-maturely, could cause a segmentation fault during cleanup.

**Fix**: (`iodine`, `fio`) fixed logging message when listening to Unix Sockets.

**Fix**: (`iodine`) fixed CLI argument recognition for WebSocket message limits and HTTP header limits. Typos in the CLI argument names prevented the CLI from effecting the default values.

**Fix**: (`fio`) fixed unaligned memory access in SipHash implementation and added secret randomization for each application restart.

**Optimization**: (`iodine`) caching common header names to decrease Ruby memory allocations per request.

#### Change log v.0.7.16

**Security**: (`fio`) security fixes from the facil.io core library (updated to 0.7.0.beta6).

**Update**: (`iodine`) better Redis support from CLI and environment (by setting the `IODINE_REDIS_URL` environment variable).

**Optimization**: (`Iodine::Mustache`) optimized worst case scenario seeking by seeking Symbols before Strings, which improved seeking times.

#### Change log v.0.7.15

**Fix**: (`fio`) fixed a minor memory leak in cluster mode, caused by the root process not freeing the hash map used for child process subscription monitoring (only effected hot restarts).

**Fix**: (`fio`) fixed superfluous and potentially erroneous pub/sub engine callback calls to `unsubscribe`, caused by (mistakingly) reporting filter channel closure.

**Fix**: (`http/1.1`) avoid processing further requests if the connection was closed.

**Fix**: (`iodine`) fixed some errors in the documentation and added a missing deprecation notice.

**Update**: (`fio`) updated the automatic concurrency calculations to leave resources for the system when a negative value is provided (was only available for worker count calculations, now available for thread count as well).

#### Change log v.0.7.14

**Fix**: (`facil.io`) fixed superfluous ping event.

**Fix**: (`iodine_tcp`) fixed responsiveness to the argument name `timeout` (a spelling mistake was testing for `timout`).

**Fix**: (`iodine_store`) fixed missing EOL marker in DEBUG messages when reporting iodine's GC guard activity.

**Update**: (`iodine`) added support for dynamic (hot) connection callback switching.

#### Change log v.0.7.13

**Fix**: (`mustache`) added support for padding in template partials.

**Fix**: (`mustache`) added support for method names as keys (i.e., `{{user.to_json}}` or `{{#user}}{{to_json}}{{/user}}`). Note: no arguments may be passed (no Ruby code parsing, just testing against method names).

#### Change log v.0.7.12

**Fix**: (`mustache`) fixed multiple issues with `Iodine::Mustache` and added lambda support for mustache templates.

#### Change log v.0.7.11

**Fix**: (`fio`) Deletes Unix sockets once done listening. Fixes an issue where the files would remain intact.

**Optimization**: (`fio`) significant memory allocation optimizations. The facil.io allocator (included with iodine) helps to protect against heap fragmentation and improves speed for concurrent memory allocations when forking / multi-threading.

#### Change log v.0.7.10

**Fix**: (pub/sub) fixed connection lock for pub/sub tasks. Now pub/sub Ruby tasks will lock the connection, protecting the user's code against concurrent access to the connection's data.

**Fix**: (installation) fixed `CFLAGS` compilation value to allow for pre-existing values set by Ruby.

**Fix**: (installation) fixed possible issues than could occur when installing iodine with  `FIO_FORCE_MALLOC`.

**Optimization**: (pub/sub) leverages facil.io broadcasting optimizations, minimizing memory allocations when broadcasting pub/sub messages directly to multiple WebSocket clients.

**Update**: (fio) updated the facil.io code to leverage it's urgent task queue for outbound IO, which minimizes reliance on the IO backup thread.

**Update**: (IO) minor tweaks to the IO backup thread and CLI output format.

#### Change log v.0.7.9

**Fix**: fixed the background IO backup thread initialization and sleep interval. This thread isn't critical. It's only used to (slowly) flush sockets when all the actual threads are blocked by long running Ruby application code.

**Feature**: added the `Iodine.worker?` and `Iodine.master?` methods, for process identification.

**Update**: Updated the automatic ActiveRecord `fork` handling code and added automatic Sequel `fork` handling, to protect against possible database communication errors related to the risk of connection sharing across worker processes.

**Update**: Moved the command line option parsing code, to leverage facil.io's `fio_cli`... It appears more flexible than Ruby's `optparse` (where command line naming is concerned).

**Deprecation**: deprecated the global namespace DSL (`after_fork`, etc'). Use the new `Iodine.on_state(:after_fork)` method instead.

#### Change log v.0.7.8

**Fix**: `unsubscribe` possibly wouldn't unsubscribe from a connection-bound subscription when instructed to do so. This was discovered during a review of a issue #45 submitted by @ojab.

**Fix**: Documentation typo fixed by @ojab in PR#46.

**Fix**: Documentation errors exposed by @ojab regarding the pub/sub specification draft and the WebSocket/SSE specification draft.

#### Change log v.0.7.7

**Fix**: (facil.io) fixed critical issue with cookies, where no more than a single cookie could be set (duplicate headers would be zeroed out before being set). Credit to @ojab for exposing the issue.

#### Change log v.0.7.6

**Fix**: (facil.io edge) timeout review was experiencing some errors that could cause timeouts to be ignored. This was fixed in the facil.io edge branch.

**Fix**: (Ruby 2.2) fixed a possible error with the Mustache parser on Ruby 2.2. I don't run Ruby 2.2, but this came up as a warning during CI tests.

**Fix**: `on_worker_boot` was mistakenly updated to a pre-fork callback (instead of a post fork callback) when attempting to fix the `on_worker_fork` behavior. This timing issue is now fixed and `on_worker_boot` is called **after** forking (both on the master process and the worker).

#### Change log v.0.7.5

**Fix**: fixed issue with direct calls to `publish` in the pre-defined pub/sub engines. These direct calls are used by custom engines when the default engine was replaced and would attempt (erroneously) direct engine access rather than use the `fio_publish` function.

**Fix**: fixed possible Array overflow risk that could result in memory corruption in some cases.

**Fix**: fixed more missing `static` keywords in the code. these should have little or no effect on the namespace (they were using long unique names with the `iodine` prefix).

#### Change log v.0.7.4

**Fix**: fixed a missing `static` keyword in the Ruby<=>C storage bridge which caused performance degradation and introduced namespace conflict risks.

**Fix**: fixed the `on_worker_fork` callback timing, to be performed before forking the process (Puma compatibility).

**Fix**: fixes to minor issues are included in the facil.io edge update.

**Optimize**: minor optimization to memory use, included in facil.io edge updates.

#### Change log v.0.7.3

**Fix**: (facil.io) updating facil.io fixes a channel name memory leak that was fixed in facil.io's edge version.

**Updated**: Improved logging for server data, allowing for total log silencing - this doesn't effect HTTP logging, only iodine's core logging system.

#### Change log v.0.7.2

**Updated**: updated the logging for HTTP services startup, to minimize log clutter.

**Feature**: (mustache) added features to `Iodine::Mustache`, to expose more of the functionality offered by facil.io.

**Fix**: (facil.io) updated from the facil.io edge (master) branch. This should fix some exposed symbols (that should have been private), minimize name-collision risks, and fix an unknown issue with the mime-type registry cleanup and other possible issues.

#### Change log v.0.7.1

**Fix**: Fixed compilation issues with older `gcc` compilers.

#### Change log v.0.7.0

This version bump is performed because the internal engine changed significantly and might be considered less mature. The public API remains unbroken. 

**Fix**: Fixed a documentation error. Credit to @Fonsan (Erik Fonselius) for PR #41.

**Feature**: (mustache) Added a bridge to facil.io's mustache template rendering engine. This isn't really a server concern, but [facil.io's C code](http://facil.io) includes this functionality anyway and it offers increased XSS protection by utilizing aggressive HTML escaping (and it's also faster than the Ruby canonical version).

**Update**: (facil.io) Updated to facil.io version 0.7.0 (edge). This could effect memory consumption behavior but otherwise shouldn't effect iodine all that much.


#### Change log v.0.6.5

**Fix**: (facil.io - logging) Fix typo in log output. Credit to @bjeanes (Bo Jeanes) for PR #39.

#### Change log v.0.6.4

**Fix**: (HTTP/WebSockets) fixed an issue where negative status return values (such as ActionCable's `-1` status code) could cause iodine to assume an abnormal error and shut down the connection. Credit to @mdesantis (Maurizio De Santis) for opening issue #38.

#### Change log v.0.6.3

**Fix**: (WebSockets) fixed an issue where WebSocket message events would attempt to create a String object outside the GVL.

**Fix**: (`Iodine::Connection`) minor updated to the documentation and memory validation system.

#### Change log v.0.6.2

**Fix**: (`Iodine::PubSub`) fixed an issue where lazy initialization would cause the shutdown process to crash if no Pub/Sub engines were ever registered (fixes an attempt to seek within an uninitialized data structure). Credit to @sj26 (Samuel Cochran) for reporting the issue.

#### Change log v.0.6.1

**Fix**: (`Iodine::PubSub`) fixed typo, `Iodine::PubSub.detach` is now correctly spelled.

**Fix**: (`Iodine::PubSub`) fix issue #37 where iodine would crash after the server's shutdown process due to Ruby Pub/Sub engines still being attached (or set as default) even after the Ruby interpreter freed all the Ruby objects. Credit to @sj26 (Samuel Cochran) for reporting the issue.

#### Change log v.0.6.0

I apologize to all my amazing early adopters for the rapid changes in the API for connection objects (SSE / WebSockets) and Pub/Sub. This was a result of an attempt to create a de-facto standard with other server authors. Hopefully the API in the 0.6.0 release will see the last of the changes.

**API BREAKING CHANGE**: The API for persistent connections (SSE / WebSockets) was drastically changed in accordance with the Rack specification discussion that required each callback to accept a "client" object (replacing the `extend` approach). Please see the documentation.

**API BREAKING CHANGE**: `Iodine.attach` was removed due to instability and issues regarding TLS/SSL and file system IO. I hope to fix these issues in a future release. For now the `Iodine.attach_fd` can be used for clear-text sockets and pipes.

**API BREAKING CHANGE**: Pub/Sub API was changed, replacing the previously suggested pub/sub object with an updated `unsubscribe` method. This means there's no need for the client to map channel names to specific subscriptions (Iodine will perform this housekeeping task for the client).

**Fix**: Iodine should now build correctly on FreeBSD. Credit to @adam12 (Adam Daniels) for detecting the issue.

---

#### Change log v.0.5.2

**Fix**: fixed compilation issues on FreeBSD. Credit to @adam12 (Adam Daniels) for opening issue #35 and offering a patch.

#### Change log v.0.5.1

**Fix**: fixed compilation issues on OS X version < 10.12 and Alpine Linux. Credit to @jdickey (Jeff Dickey) for opening issue #32.

**Fix**: fixed some documentation errors. Credit to @janko-m (Janko Marohnić) for catching typos in the README.

#### Change log v.0.5.0

Changed... everything. At least all the internal bits and some of the API.

Iodine 0.5.0 is a stability oriented release. It also supports the updated Rack specification draft for WebSocket and SSE connections (yes, iodine 0.5.0 brings about SSE support).

Deprecated the `each` function family in favor of the more scalable pub/sub approach.

Moved the HTTP network layer outside of the GIL, more robust pub/sub (using Unix Sockets instead of pipes), hot restart (in cluster mode) and more.

Larger header support. The total headers length now defaults to 32Kb, but can be adjusted. A hard coded limit of 8Kb per header line is still enforced (to minimize network buffer).

Improved concurrency and energy consumption (idling CPU cycles reduced).

Higher overall memory consumption might be observed (some security and network features now perform data copying rather than allowing for direct data access).

Improved automatic header completion for missing `Content-Length`, `Date` and `Last-Modified`.

Support for the Unicorn style `before_fork` and `after_fork` DSL as well as the Puma style `on_worker_boot` DSL.

Credit to Anatoly Nosov (@jomei) for fixing some typos in the documentation.

---

#### Change log v.0.4.19

**Feature**: (`iodine`) added requested feature in issue #27, `Iodine.running?` will return Iodine's state.

#### Change log v.0.4.18

**Fix**: (`iodine pub/bus`) fixed issue #27 (?) where the `block` used for subscriptions would be recycled by the GC and the memory address (retained by `iodine`) would point at invalid Ruby objects (at worst) or become invalid (at best). Credit to Dmitry Davydov (@haukot) for exposing this issue.

**Fix**: (`facil pub/bus`) fixed issue #27 (?) where the `memcpy` was used instead of `memmove`, resulting in possibly corrupt data in cluster messaging system. Credit to Dmitry Davydov (@haukot) for exposing this issue.

#### Change log v.0.4.17

**Fix**: (`iodine RubyCaller`) fixed issue #26 that exposed an issue in the exception handling logic. This fix enforces exception handling whenever entering the Ruby GVL (GIL), allowing C functions to safely enter the user's Ruby code (where before C functions were assumed to be safe and user code would be executed unprotected when routed through certain functions). Credit to Dmitry Davydov (@haukot) for exposing this issue (issue #26).

#### Change log v.0.4.16

**Fix**: (`websocket_parser`) The websocket parser had a memory offset and alignment handling issue in it's unmasking (XOR) logic and the new memory alignment protection code. The issue would impact the parser in rare occasions when multiple messages where pipelined in the internal buffer and their length produced an odd alignment (the issue would occur with very fast clients, or a very stressed server).

#### Change log v.0.4.15

**Update**: (`facil.io`) updating the facil.io library version to use the 0.5.8 released version.

This includes the following changes (as well as other minor changes), as detailed in facil.io's CHANGELOG:

**Compatibility**: (`websocket_parser`) removed unaligned memory access from the XOR logic in the parser, making it more compatible with older CPU systems that don't support unaligned memory access or 64 bit word lengths.

**Optimization**: (`defer`) rewrote the data structure to use a hybrid cyclic buffer and linked list for the task queue (instead of a simple linked list), optimizing locality and minimizing memory allocations.

**Compatibility**: (`gcc-6`) Fix some compatibility concerns with `gcc` version 6, as well as some warnings that were exposed when testing with `gcc`.

---

#### Change log v.0.4.14

**Fix**: (`facil.io`) fixes an issue where timer monitoring would report failure when the timer exists and is being monitored.

---

#### Change log v.0.4.12

**Fix**: (`facil.io`) fixes some lingering issues with the new Websocket parser, namely an issue where certain network packet lengths would cause the parser to fail. Credit to Tom Lahti (@uidzip) for exposing the issue.

---

#### Change log v.0.4.11

**Fix**: (`iodine`) use Ruby `fork` instead of system `fork`, allowing Ruby hooks to run before and after forking. This also fixes an issue where the Ruby timer thread isn't (re)initialized.

---

#### Change log v.0.4.10

**Portability**: (`mac OS High Sierra`) iodine will load the Objective C library on macOS machines before starting up the server - this will prevent `fork` from crashing the server on macOS High Sierra, see [discussion here](https://github.com/puma/puma/issues/1421).

**Fix**: (`facil.io`) fixes an error with the new Websocket parser (introduced in v. 0.4.9) that caused medium sized messages (127 Bytes - 64Kib) to be parsed incorrectly. Apologies. The test program I used seems to have validated messages using length comparison (instead of data comparison). Credit to Tom Lahti (@uidzip) for exposing the issue.

---

#### Change log v.0.4.9

**Change**: (`facil.io`) the internal Websocket parser was replaced with something easier to read, for maintainability reasons. Performance seems to be mostly unaffected (sometimes it's faster and sometimes it's slower, common case is slightly optimized).

**Change**: (`facil.io`) iodine will compile facil.io with the `NO_CHILD_REAPER` flag, in order to workaround the Rails ExecJS gem that [assumes no child reaping is performed](https://github.com/rails/execjs/issues/68). This workaround is, hopefully, temporary. Credit to @jerryshen for exposing the issue.

**Fix**: (`Iodine`) test for timer creation error in `run_after` and `run_every`.

**Fix**: (`facil.io`) timer creation now correctly detects if the reactor was stopped, allowing the creation of new timers before the reactor's reactivation.

**Fix**: (`facil.io`) timer timeout review is now correctly ignored, preventing the timer from being shut down prematurely.

---

#### Change log v.0.4.8

**Change**: (`facil.io`) the internal HTTP parser was replaced with something easier to read, for maintainability reasons. Performance seems to be unaffected.

**Fix**: HTTP request logging included an extra info line which was a debug/testing message inherited from `facil.io` v.0.5.3-pre-release. This is now removed.

**Performance**: The `now` HTTP Date string is now cached for up to 2 seconds, improving performance for `Date`, `Last-Modified` and Iodine logging messages that relate to the current time. However, it's likely that Rack will write it's own date string, masking this feature.

---

#### Change log v.0.4.7

**Update**: Now using `facil.io` edge (stripped down v.0.5.3).

**Fix**: (`websocket`) fix #21, where a client's first message could have been lost due to long `on_open` processing times. This was fixed by fragmenting the `upgrade` event into two events, adding the `facil_attach_locked` feature and attaching the new protocol before sending the response. Credit to @madsheep and @nilclass for exposing the issue and tracking it down to the `on_open` callbacks.

**Fix**: (`sock`) sockets created using the TCP/IP `sock` library now use `TCP_NODELAY` as the new default. This shouldn't be considered a breaking change as much as it should be considered a fix.

**Fix**: (`http1`) HTTP/1.x now correctly initializes the `udata` pointer to NULL fore each new request.

**Fix**: (`defer`) a shutdown issue in `defer_perform_in_fork` was detected by @cdkrot and his fix was implemented.

---

#### Change log v.0.4.6

**Update**: Now using `facil.io` v.0.5.2.

**Fix**: (from `facil.io`) fix `SIGTERM` handling, make sure sibling processes exit when a sibling dies.

---

#### Change log v.0.4.5

**Fix**: fix static file service for `X-Sendfile` as well as static error file fallback pages (404.html etc').

---

#### Change log v.0.4.4

**Fix**: fixed an issue related to Ruby 2.3 optimizations of String management (an issue that didn't seem to effect Ruby 2.4). This fix disables the recyclable buffer implemented for the `on_message` Websocket callback. The callback will now receive a copy of the buffer (not the buffer itself), so there is no risk of collisions between the network buffer (managed in C) and the `on_message(data)` String (managed by Ruby).

---

#### Change log v.0.4.3

**Fix**: fixed a possible issue in fragmented pipelined Websocket messages.

---

#### Change log v.0.4.2

**Fix**: fixed an issue where Websocket `ping` timeouts were being ignored for the default `Iodine::Rack` server, causing the default (40 seconds) to persist over specified valued.

**Fix**: fixed a possible issue with high-jacking which might cause the server to hang.

---

#### Change log v.0.4.1

**Fix**: postpone warmup in fear of abuse and collisions when using `fork`. i.e., during warmup, an application might perform actions that conflict with `fork` and worker initialization, such as creating a database connection pool during warmup, or maybe spawning a thread. Now `warmup` is postponed until *after* worker processes are up and running, resulting in a per-process warmup rather than a per-cluster warmup.

**Fix** move the `rake-compiler` dependency to "development" instead of "runtime". Credit to Luis Lavena (@luislavena) for exposing the issue (#19).

---

#### Change log v.0.4.0

**Braking change**: Some of the API was changed / updated, hopefully simplified.

**DEPRECTAION / Braking change**: The `websocket#write_each` function is gone. Future (planned) support for a Pub/Sub API would have caused confusion and since it's barely used (probably only there as a benchmarking proof of concept) it was removed.

**Update**: Now using `facil.io` v.0.5.0 The extended features include the following listed features.

**Fixes**: This was such a big rewrite, I suspect half the fixes I want to list are things I broke during the rewrite... but there were plenty of fixes.

**Feature**: Iodine now support native Websocket Pub/Sub, with [an example in the `examples` folder](./examples/redis.ru). i.e.:

```ruby
# Within a Websocket connection:
subscribe "chat"
publish "chat", "Iodine is here!"
```

**Feature**: Iodine's Pub/Sub API supports both direct client messages and server filtered messages. i.e.

```ruby
# Within a Websocket connection:
subscribe("chat-server") {|msg| write "Notice: #{msg}" }
# v.s
subscribe("chat-client")

publish "chat-server", "Iodine is here!"
```

**Feature**: Iodine's Pub/Sub API includes support for home made Pub/Sub Engines connecting Iodine to your Pub/Sub service of choice.

**Feature**: Iodine's Pub/Sub support includes a Process Cluster engine (pub/sub to all Websockets sharing the process cluster) as well as a Single Process engine (pub/sub to all websockets supporting a single process).

**Feature**: Iodine's Pub/Sub support includes a native Redis Pub/Sub engine. The parser is written from the ground up in C to keep the Iodine licensing as MIT. It's young, so keep your eyes pealed and submit any issues you encounter.

**Feature + Breaking Change**: Iodine now support multiple HTTP servers at once. i.e.:

```ruby
# `Iodine::HTTP.listen` initializes an HTTP service in the C and system levels, so it can't be changed once initialized.
Iodine::HTTP.listen port: 3000, app: my_app1
Iodine::HTTP.listen port: 3000, app: my_app2, public: "./www"
Iodine.start
```

---

#### Change log v.0.3.6

**Update**: Now using `facil.io` v.0.4.3. This fixes some delays in websocket packet flushing (latency), as well as other internal polishes. It also promises some possible future feature extensions that could add a major performance boost.

---

#### Change log v.0.3.5

**Fix**: (`sock`) Fixed an issue with the `sendfile` implementation on macOS and BSD, where medium to large files wouldn't be sent correctly.

**Minor changes**: This release incorporates some more minor changes from the [`facil.io` 0.4.2 update](https://github.com/boazsegev/facil.io/releases/tag/v.0.4.2).

---

#### Change log v.0.3.4

**Fix**: (`sock`, `facil`, bscrypt) Add missing `static` keywords to prevent state collisions with other libraries.

---

#### Change log v.0.3.3

**Update**: Now using `facil.io` v.0.4.1

**Fix**: (from `facil.io`) fixed the default response `Date` (should have been "now", but wasn't initialized).

**Compatibility**: (from `facil.io`) Now checks for HTTP/1.0 clients to determine connection persistence.

**Compatibility**: (from `facil.io`) Added spaces after header names (`:` => `: `), since some parsers don't seem to read the RFC.

---

#### Change log v.0.3.2

**Fix**: (from `facil.io`) fixed thread throttling for better energy conservation.

**Fix**: (from `facil.io`) fixed stream response logging.

---

#### Change log v.0.3.1

**Update**: Follow `facil.io`'s update for healthier thread throttling and energy consumption.
---

#### Change log v.0.3.1

**Fix**: Fixed a minor issue with the logging of responses where the size of the response is unknown (streamed).

**Gem Specification update**: We updated the gem specification to allow for Rack 1.x users and to update the gem description.

---

#### Change log v.0.3.0

**`facil.io` C Core Update**: The C library core that drives Iodine [`facil.io`](http://facil.io) was updated to version 0.4.0 and Iodine follows closely on the heels of this update. The transition was easy enough and the API remains unchanged... but because the performance gain was so big and because it's a new code base, we opted to bump the minor release version.

---

#### Change log v.0.2.17

**Performance**: Enhanced Performance for single threaded / blocking applications by adding a dedicated IO thread. This is related to issue #14.

---

#### Change log v.0.2.16

**Update**: iodine can now run as a basic HTTP static file server without a Ruby application (no `config.ru`) when the `-www` option is used from the command line.

---

#### Change log v.0.2.15

**Fix**: Fixed typo in logging and code comments, credit to @jmoriau in PR #13.

---

#### Change log v.0.2.14

**Fix**: fixed the experimental `each_write`. An issue was found where passing a block might crash Iodine, since the block will be freed by the GC before Iodine was done with it. Now the block is correctly added to the object Registry, preventing premature memory deallocation.

**Fix**: fixed another issue with `each_write` where a race condition review was performed outside the protected critical section, in some cases this would caused memory to be freed twice and crash the server. This issue is now resolved.

**Deprecation**: In version 0.2.1 we have notified that the the Websocket method `uuid` was deprecated in favor of `conn_id`, as suggested by the [Rack Websocket Draft](https://github.com/rack/rack/pull/1107). This deprecation is now enforced.

---

#### Change log v.0.2.13

**Fix**: Fixed an issue presented in the C layer, where big fragmented Websocket messages sent by the client could cause parsing errors and potentially, in some cases, cause a server thread to spin in a loop (DoS). Credit to @Filly for exposing the issue in the [`facil.io`](https://github.com/boazsegev/facil.io) layer. It should be noted that Chrome is the only browser where this issue could be invoked for testing.

**Credit**: credit to Elia Schito (@elia) and Augusts Bautra (@Epigene) for fixing parts of the documentation (PR #11 , #12).

---

#### Change log v.0.2.12

**Fix**: removed `mempool` after it failed some stress and concurrency tests.

---

#### Change log v.0.2.11

**Fix**: C layer memory pool had a race-condition that could have caused, in some unlikely events, memory allocation failure for Websocket protocol handlers. This had now been addressed and fixed.

**Experimental feature**: added an `each_write` feature to allow direct `write` operations that write data to all open Websocket connections sharing the same process (worker). When this method is called without the optional block, the data will be sent without the need to acquire the Ruby GIL.

**Update**: lessons learned from `facil.io` have been implemented for better compatibility of Iodine's core C layer.

---

#### Change log v.0.2.10

**Update**: added documentation and an extra helper method to set a connection's timeout when using custom protocols (Iodine as an EventMachine alternative).

**C Layer Update** updated the [`facil.io`](http://facil.io) library used, to incorporate the following fixes / update:

* Better cross platform compilation by avoiding some name-space clashes. i.e, fixes a name clash with the `__used` directive / macro, where some OSs (i.e. CentOS) used a similar directive with different semantics.

* Reviewed and fixed some signed vs. unsigned integer comparisons.

* Smoother event scheduling by increasing the event-node's pool size.

* Smoother thread concurrency growth by managing thread `nanosleep` times as thread count dependent.

* Cleared out "unused variable" warnings.

* Streamlined the `accept` process to remove a double socket's data clean-up.

* `SERVER_DELAY_IO` is now implemented as an event instead of a stack frame.

* Fixed a possible Linux `sendfile` implementation issue where sometimes errors wouldn't be caught or `sendfile` would be called past a file's limit (edge case handling).

* `bscrypt` random generator (where `dev/random` is unavailable) should now provide more entropy.


---

#### Change log v.0.2.9

**Fix**: fixed a gcc-4.8 compatibility issue that prevented iodine 0.2.8 from compiling on Heroku's cedar-14 stack. This was related to missing system include files in gcc-4.8. It should be noted that Heroku's stack and compiler (which utilizes Ubuntu 14) has known issues and / or limited support for some of it's published features... but I should have remembered that before releasing version 0.2.8... sorry about that.

---

#### Change log v.0.2.8

**Memory Performance**: The Websocket connection Protocol now utilizes both a C level memory pool and a local thread storage for temporary data. This helps mitigate possible memory fragmentation issues related to long running processes and long-lived objects. In addition, the socket `read` buffer was moved from the protocol object to a local thread storage (assumes pthreads and not green threads). This minimizes the memory footprint for each connection (at the expense of memory locality) and should allow Iodine to support more concurrent connections using less system resources. Last, but not least, the default message buffer per connection starts at 4Kb instead of 16Kb (grows as needed, up to `Iodine::Rack.max_msg_size`), assuming smaller messages are the norm.

**Housekeeping**: Cleaned up some code, removed old files, copied over the latest [`facil.io`](http://facil.io) library. There's probably some more housekeeping left to perform, especially anywhere where documentation is concerned. I welcome help with documentation.

---

#### Change log v.0.2.7

**Minor Fix**: fixed an issue where a negative number of processes or threads would initiate a very large number of forks, promoting a system resource choke. Limited the number of threads (1023) and processes (127).

**Update**: Automated the number of processes (forks) and threads used when these are not explicitly specified. These follow the number of cores / 2.

---

#### Change log v.0.2.6

**Update**: The IO reactor review will now be delayed until all events scheduled are done. This means that is events schedule future events, no IO data will be reviewed until all scheduled data is done. Foolish use might cause infinite loops that skip the IO reactor, but otherwise performance is improved (since the IO reactor might cause a thread to "sleep", delaying event execution).

---

#### Change log v.0.2.5

**Fix:**: fix for issue #9 (credit to Jack Christensen for exposing the issue) caused by an unlocked critical section's "window of opportunity" that allowed asynchronous Websocket `each` blocks to run during the tail of the Websocket handshake (while the `on_open` callback was running in parallel).

**Minor Fix**: Fix Iodine::Rack's startup message's `fprintf` call to fit correct argument sizes (Linux warnings).

---

#### Change log v.0.2.4

**Minor Fix**: Patched Iodine against Apple's broken `getrlimit` on macOS. This allows correct auto-setting of open file limits for the socket layer.

**Minor Fix**: Fixed the processor under-utilization warning, where "0" might be shown for the number processes instead of "1".

**Update**: Added support for the `env` keys `HTTP_VERSION` and `SERVER_PROTOCOL` to indicate the HTTP protocol version. Iodine implements an HTTP/1.1 server, so versions aren't expected to be higher than 1.x.

**Update**: Iodine::Rack startup messages now include details regarding open file limits imposed by the OS (open file limits control the maximum allowed concurrent connections and other resource limits).

---

#### Change log v.0.2.3

**Update**: The `write` system call is now deferred when resources allow, meaning that (as long as the `write` buffer isn't full) `write` is not only non-blocking, but it's performed as a separate event, outside of the Ruby GIL.

**Update**: The global socket `write` buffer was increased to ~16Mb (from ~4Mb), allowing for more concurrent `write` operations. However, the `write` buffer is still limited and `write` might block while the buffer is full. Blocking and "hanging" the server until there's enough room in the buffer for the requested `write` will slow the server down while keeping it healthy and more secure. IMHO, it is the lesser of two evils.

---

#### Change log v.0.2.2

**Update** The static file service now supports `ETag` caching, sending a 304 (not changed) response for valid ETags.

**Update**: A performance warning now shows if the CPUs are significantly under-utilized (less than half are used) of if too many are utilized (more than double the amount of CPUs), warning against under-utilization or excessive context switching (respectively).

---

#### Change log v.0.2.1

**Notice**: The [Rack Websocket Draft](https://github.com/rack/rack/pull/1107) does not support the `each` and `defer` methods. Although I tried to maintain these as part of the draft, the community preferred to leave the implementation of these to the client (rather then the server). If collisions occur, these methods might be removed in the future.

**Update**: Websockets now support the `has_pending?` method and `on_ready` callback, as suggested by the [Rack Websocket Draft](https://github.com/rack/rack/pull/1107).

**Update**: deprecated the Websocket method `uuid` in favor of `conn_id`, as suggested by the [Rack Websocket Draft](https://github.com/rack/rack/pull/1107).

**Fix**: fixed an issue were the server would crash when attempting to send a long enough websocket message.

---

#### Change log v.0.2.0

This version is a total rewrite. The API is totally changed, nothing stayed.

Iodine is now written in C, as a C extension for Ruby. The little, if any, ruby code written is just the fluff and feathers.

---

### deprecation of the 0.1.x version line

#### Change log v.0.1.21

**Optimization**: Minor optimizations. i.e. - creates 1 less Time object per request (The logging still creates a Time object unless disabled using `Iodine.logger = nil`).

**Security**: HTTP/1 now reviews the Body's size as it grows (similar to HTTP/2), mitigating any potential attacks related to the size of the data sent.

**Logs**: Log the number of threads utilized when starting up the server.

---

#### Change log v.0.1.20

**Update/Fix**: Updated the `x-forwarded-for` header recognition, to accommodate an Array formatting sometimes used (`["ip1", "ip2", ...]`).

**Update**: native support for the `Forwarded` header HTTP.

**API Changes**: `Iodine::HTTP.max_http_buffer` was replaced with `Iodine::HTTP.max_body_size`, for a better understanding of the method's result.

---

#### Change log v.0.1.19

**Update**: added the `go_away` method to the HTTP/1 protocol, for seamless connection closeing across HTTP/2, HTTP/1 and Websockets.

---

#### Change log v.0.1.18

**Update**: The request now has the shortcut method `Request#host_name` for accessing the host's name (without the port part of the string).

---

#### Change log v.0.1.17

**Credit**: thanks you @frozenfoxx for going through the readme and fixing my broken grammar.

**Fix**: fixed an issue where multiple Pings might get sent when pinging takes time. Now pings are exclusive (run within their own Mutex).

**Fix**: HTTP/2 is back... sorry about breaking it in the 0.1.16 version. When I updated the write buffer I forgot to write the status of the response, causing a protocol error related with the headers. It's now working again.

**Update**: by default and for security reasons, session id's created through a secure connection (SSL) will NOT be available on a non secure connection (SSL/TLS). However, while upgrading to the encrypted connection, the non_encrypted session storage is now available for review using the `Response#session_old` method.

* Remember that sessions are never really safe, no matter how much we guard them. Session hijacking is far too easy. This is why Iodine stores the session data locally and not within the session cookie. This is also why you should review any authentication before performing sensitive tasks based on session stored authentication data.

---

#### Change log v.0.1.16

**Performance**: HTTP/1 and HTTP/2 connections now share and recycle their write buffer when while reading the response body and writing it to the IO. This (hopefully) prevents excess `malloc` calls by the interpreter.

---

#### Change log v.0.1.15

**Update**: IO reactor will now update IO status even when tasks are pending. IO will still be read only when there are no more tasks to handle, but this allows chained tasks to relate to the updated IO status. i.e. this should improve Websocket availability for broadcasting (delay from connection to availability might occur until IO is registered).

**Update**: Websockets now support the `on_ping` callback, which will be called whenever a ping was sent without error.

---

#### Change log v.0.1.14

**Update**: the Response now supports `redirect_to` for both permanent and temporary redirection, with an optional `flash` cookie setup.

**Performance**: the Protocol class now recycles the data string as a thread global socket buffer (different threads have different buffer strings), preventing excessive `malloc` calls by the Ruby interpreter. To keep the `data` (in `on_message(data)`) past the `on_message` method's scope, be sure to duplicate it using `data.dup`, or the string's buffer will be recycled.

---

#### Change log v.0.1.13

**Change**: Session cookie lifetime is now limited to the browser's session. The local data will still persist until the tmp-folder is cleared (when using session file storage).

**Fix**: renamed the SSL session token so that the SSL session id isn't lost when a non-secure session is used.

**Fix**: The `flash` cookie-jar will now actively prevent Symbol and String keys from overlapping.

**Compatibility**: minor fixes and changes in preparation for Ruby 2.3.0. These may affect performance due to slower String initialization times.

---

#### Change log v.0.1.12

**Update**: Passing a hash as the cookie value will allow to set cookie parameters using the {Response#set_cookie} options. i.e.: `cookies['key']= {value: "lock", max_age: 20}`.

**Security**: set the HTTPOnly flag for session id cookies.

---

#### Change log v.0.1.11

**Fix**: fixed the Rack server Handler, which was broken in version 0.1.10.

---

#### Change log v.0.1.10

**Fix**: make sure the WebsocketClient doesn't automatically renew the connection when the connection was manually closed by the client.

**Performance**: faster TimedEvent clearing when manually stopped. Minor improvements to direct big-file sending (recycle buffer to avoid `malloc`).

---

#### Change log v.0.1.9

**Fix**: WebsocketClient connection renewal will now keep the same WebsocketClient instance object.

**Update** Creating a TimedEvent before Iodine starts running will automatically 'nudge' Iodine into "Task polling" mode, cycling until the user signals a stop.

**Update**: repeatedly calling `Iodine.force_start!` will now be ignored, as might have been expected. Once Iodine had started, `force_start!` cannot be called until Iodine had finished (and even than, Iodine might never be as fresh nor as young as it was).

---

#### Change log v.0.1.8

**Fix**: Websocket broadcasts are now correctly executed within the IO's mutex locker. This maintains the idea that only one thread at a time should be executing code on behalf of any given Protocol object ("yes" to concurrency between objects but "no" to concurrency within objects).

**Fix** fixed an issue where manually setting the number of threads for Rack applications (when using Iodine as a Rack server), the setting was mistakenly ignored.

**Fix** fixed an issue where sometimes extracting the HTTP response's body would fail (if body is `nil`).

**Feature**: session objects are now aware of the session id. The session id is available by calling `response.session.id`

**Fix** fixed an issue where HTTP streaming wasn't chunk encoding after connection error handling update.

**Fix** fixed an issue where HTTP streaming would disconnect while still processing. Streaming timeout now extended to 15 seconds between response writes.

---

#### Change log v.0.1.7

Removed a deprecation notice for blocking API. Client API will remain blocking due to use-case requirements.

---

#### Change log v.0.1.6

**Fix**: fixed an issue where a session key-value pair might not get deleted when using `session.delete key` and the `key` is not a String object. Also, now setting a key's value to `nil` should delete the key-value pair.

**Fix**: fixed an issue where WebsocketClient wouldn't mask outgoing data, causing some servers to respond badly.

**Performance**: minor performance improvements to the Websocket parser, for unmasking messages.

**Deprecation notice**:

(removed after reviewing use-cases).

---

#### Change log v.0.1.5

**Feature**: The Response#body can now be set to a File object, allowing Iodine to preserve memory when serving large static files from disc. Limited Range requests are also supported - together, these changes allow Iodine to serve media files (such as movies) while suffering a smaller memory penalty and supporting a wider variety of players (Safari requires Range request support for it's media player).

**Fix**: Fixed an issue where Iodine might take a long time to shut down after a Fatal Error during the server initialization.

---

#### Change log v.0.1.4

**Fix**: fixed an issue with where the WebsocketClient#on_close wouldn't be called for a renewable Websocket connection during shutdown.

**Fix**: fixed an issue where a protocol's #on_close callback wouldn't be called if the Iodine server receives a shutdown signal.

**Fix**: fixed an issue where HTTP2 header size limit condition was not recognized by the Ruby parser (a double space issue, might be an issue with the 2.2.3 Ruby parser).

---

#### Change log v.0.1.3

**Fix**: fixed an issue with the new form/multipart parser, where the '+' sign would be converted to spaces on form fields (not uploaded files), causing in-advert potential change to the original POSTed data.

---

#### Change log v.0.1.2

**Fix**: fixed an issue where the default implementation of `ping` did not reset the timeout if the connection wasn't being closed (the default implementation checks if the Protocol is working on existing data and either resets the timer allowing the work to complete or closes the connection if no work is being done).

---

#### Change log v.0.1.1

**Fix**: Fixed an issue where slow processing of HTTP/1 requests could cause timeout disconnections to occur while the request is being processed.

**Change/Security**: Uploads now use temporary files. Aceessing the data for file uploads should be done throught the `:file` property of the params hash (i.e. `params[:upload_field_name][:file]`). Using the `:data` property (old API) would cause the whole file to be dumped to the memory and the file's content will be returned as a String.

**Change/Security**: HTTP upload limits are now enforced. The current default limit is about ~0.5GB.

**Feature**: WebsocketClient now supports both an auto-connection-renewal and a polling machanism built in to the `WebsocketClient.connect` API. The polling feature is mostly a handy helper for testing, as it is assumed that connection renewal and pub/sub offer a better design than polling.

**Logging**: Better HTTP error logging and recognition.

---

#### Change log v.0.1.0

**First actual release**:

We learn, we evolve, we change... but we remember our past and do our best to help with the transition and make it worth the toll it takes on our resources.

I took much of the code used for GRHTTP and GReactor, changed it, morphed it and united it into the singular Iodine gem. This includes Major API changes, refactoring of code, bug fixes and changes to the core approach of how a task/io based application should behave or be constructed.

For example, Iodine kicks in automatically when the setup script is done, so that all code is run from within tasks and IO connections and no code is run in parallel to the Iodine engine.

Another example, Iodine now favors Object Oriented code, so that some actions - such as writing a network service - require classes of objects to be declared or inherited (i.e. the Protocol class).

This allows objects to manage their data as if they were in a single thread environment, unless the objects themselves are calling asynchronous code. For example, the Protocol class makes sure that the `on_open` and `on_message(data)` callbacks are executed within a Mutex (`on_close` is an exception to the rule since it is assumed that objects should be prepared to loose network connection at any moment).

Another example is that real-life deployment preferences were favored over adjustability or features. This means that some command-line arguments are automatically recognized (such as the `-p <port>` argument) and that Iodine assumes a single web service per script/process (whereas GReactor and GRHTTP allowed multiple listening sockets).

I tested this new gem during the 0.0.x version releases, and I feel that version 0.1.0 is stable enough to work with. For instance, I left the Iodine server running all night under stress (repeatedly benchmarking it)... millions of requests later, under heavy load, a restart wasn't required and memory consumption didn't show any increase after the warmup period.



## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).
