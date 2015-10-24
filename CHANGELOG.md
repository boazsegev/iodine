# Iodine
[![Gem Version](https://badge.fury.io/rb/iodine.svg)](https://badge.fury.io/rb/iodine)
[![Inline docs](http://inch-ci.org/github/boazsegev/iodine.svg?branch=master)](http://www.rubydoc.info/github/boazsegev/iodine/master/frames)

Please notice that this change log contains changes for upcoming releases as well. Please refer to the current gem version to review the current release.

## Changes:

***

Change log v.0.1.2

**Fix**: fixed an issue where the default implementation of `ping` didn not reset the timeout if the connection wasn't being closed (the default implementation checks if the Protocol is working on existing data and either resets the timer allowing the work to complete or closes the connection if no work is being done).

***

Change log v.0.1.1

**Fix**: Fixed an issue where slow processing of Http/1 requests could cause timeout disconnections to occur while the request is being processed.

**Change/Security**: Uploads now use temporary files. Aceessing the data for file uploads should be done throught the `:file` property of the params hash (i.e. `params[:upload_field_name][:file]`). Using the `:data` property (old API) would cause the whole file to be dumped to the memory and the file's content will be returned as a String.

**Change/Security**: Http upload limits are now enforced. The current default limit is about ~0.5GB.

**Feature**: WebsocketClient now supports both an auto-connection-renewal and a polling machanism built in to the `WebsocketClient.connect` API. The polling feature is mostly a handy helper for testing, as it is assumed that connection renewal and pub/sub offer a better design than polling.

**Logging**: Better Http error logging and recognition.

***

Change log v.0.1.0

**First actual release**:

We learn, we evolve, we change... but we remember our past and do our best to help with the transition and make it worth the toll it takes on our resources.

I took much of the code used for GRHttp and GReactor, changed it, morphed it and united it into the singular Iodine gem. This includes Major API changes, refactoring of code, bug fixes and changes to the core approach of how a task/io based application should behave or be constructed.

For example, Iodine kicks in automatically when the setup script is done, so that all code is run from within tasks and IO connections and no code is run in parallel to the Iodine engine.

Another example, Iodine now favors Object Oriented code, so that some actions - such as writing a network service - require classes of objects to be declared or inherited (i.e. the Protocol class).

This allows objects to manage their data as if they were in a single thread environment, unless the objects themselves are calling asynchronous code. For example, the Protocol class makes sure that the `on_open` and `on_message(data)` callbacks are excecuted within a Mutex (`on_close` is an exception to the rule since it is assumed that objects should be prepared to loose network connection at any moment).

Another example is that real-life deployemnt preferences were favored over adjustability or features. This means that some command-line arguments are automatically recognized (such as the `-p <port>` argument) and thet Iodine assumes a single web service per script/process (whereas GReactor and GRHttp allowed multiple listening sockets).

I tested this new gem during the 0.0.x version releases, and I feel that version 0.1.0 is stable enough to work with. For instance, I left the Iodine server running all night under stress (repeatedly benchmarking it)... millions of requests later, under heavey load, a restart wasn't required and memory consumption didn't show any increase after the warmup period.



## License

The gem is available as open source under the terms of the [MIT License](http://opensource.org/licenses/MIT).

