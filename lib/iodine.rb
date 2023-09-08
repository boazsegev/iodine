# frozen_string_literal: true

require 'stringio' # Used internally as a default RackIO
require 'socket'  # TCPSocket is used internally for Hijack support

require_relative "iodine/version"

require "iodine/iodine_ext"

# Iodine is an HTTP / WebSocket server as well as an Evented Network Tool Library. In essense, Iodine is a Ruby port for the [facil.io](http://facil.io) C library.
#
# Here is a simple telnet based echo server using Iodine (see full list at {Iodine::Connection}):
#
#
#       require 'iodine'
#       # define the protocol for our service
#       module EchoProtocol
#         def on_open(client)
#           # Set a connection timeout
#           client.timeout = 10
#           # Write a welcome message
#           client.write "Echo server running on Iodine #{Iodine::VERSION}.\r\n"
#         end
#         # this is called for incoming data - note data might be fragmented.
#         def on_message(client, data)
#           # write the data we received
#           client.write "echo: #{data}"
#           # close the connection when the time comes
#           client.close if data =~ /^bye[\n\r]/
#         end
#         # called if the connection is still open and the server is shutting down.
#         def on_shutdown(client)
#           # write the data we received
#           client.write "Server going away\r\n"
#         end
#         extend self
#       end
#       # create the service instance, the block returns a connection handler.
#       Iodine.listen(port: "3000") { EchoProtocol }
#       # start the service
#       Iodine.threads = 1
#       Iodine.start
#
#
#
# Methods for setting up and starting {Iodine} include {start}, {threads}, {threads=}, {workers} and {workers=}.
#
# Methods for setting startup / operational callbacks include {on_idle}, {on_state}.
#
# Methods for asynchronous execution include {run} (same as {defer}), {run_after} and {run_every}.
#
# Methods for application wide pub/sub include {subscribe}, {unsubscribe} and {publish}. Connection specific pub/sub methods are documented in the {Iodine::Connection} class).
#
# Methods for TCP/IP, Unix Sockets and HTTP connections include {listen} and {connect}.
#
# Note that the HTTP server supports both TCP/IP and Unix Sockets as well as SSE / WebSockets extensions.
#
# Iodine doesn't call {patch_rack} automatically, but doing so will improve Rack's performace.
#
# Please read the {file:README.md} file for an introduction to Iodine.
#
module Iodine

# {Iodine::Mustache} is a lighter implementation of the mustache template rendering gem, with a focus on a few minor security details:
# 
# 1. HTML escaping is more aggressive, increasing XSS protection. Read why at: [wonko.com/post/html-escaping](https://wonko.com/post/html-escaping).
# 
# 2. Dot notation is tested in whole as well as in part (i.e. `user.name.first` will be tested as is, than the couplet `user`, `name.first` and than as each `user`, `name` , `first`), allowing for the Hash data to contain keys with dots while still supporting dot notation shortcuts.
# 
# 3. Less logic: i.e., lambdas / procs do not automatically invoke a re-rendering... I'd remove them completely as unsafe, but for now there's that.
# 
# 4. Improved Protection against Endless Recursion: i.e., Partial templates reference themselves when recursively nested (instead of being recursively re-loaded); and Partial's context is limited to their starting point's context (cannot access parent context).
# 
# It wasn't designed specifically for speed or performance... but it ended up being significantly faster.
# 
# ## Usage
# 
# This approach to Mustache templates may require more forethought when designing either the template or the context's data format, however it should force implementations to be more secure and performance aware.
# 
# Approach:
# 
# ```ruby
# require 'iodine'
# # One-off rendering of (possibly dynamic) template:
# result = Iodine::Mustache.render(template: "{{foo}}", ctx: {foo: "bar"}) # => "bar"
# # caching of parsed template data for multiple render operations:
# view = Iodine::Mustache.new(file: "./views/foo.mustache", template: "{{foo}}")
# results = Array.new(100) {|i| view.render(foo: "bar#{i}") } # => ["bar0", "bar1", ...]
# ```
# 
# ## Performance
# 
# Performance on my machine seems very fast
# 
# ```ruby
# require 'benchmark/ips'
# require 'mustache'
# require 'iodine'
# 
# # Benchmark code was copied, in part, from:
# #   https://github.com/mustache/mustache/blob/master/benchmarks/render_collection_benchmark.rb
# # The test is, sadly, biased and doesn't test for missing elements, proc/method resolution or template partials.
# def benchmark_mustache
#   template = """
#   {{#products}}
#     <div class='product_brick'>
#       <div class='container'>
#         <div class='element'>
#           <img src='images/{{image}}' class='product_miniature' />
#         </div>
#         <div class='element description'>
#           <a href={{url}} class='product_name block bold'>
#             {{external_index}}
#           </a>
#         </div>
#       </div>
#     </div>
#   {{/products}}
#   """
#   
#   # fill Hash objects with values for template rendering
#   data_1000 = {
#     products: []
#   }
#   data_1000_escaped = {
#     products: []
#   }
# 
#   1000.times do
#     data_1000[:products] << {
#       :external_index=>"product",
#       :url=>"/products/7",
#       :image=>"products/product.jpg"
#     }
#     data_1000_escaped[:products] << {
#       :external_index=>"This <product> should've been \"properly\" escaped.",
#       :url=>"/products/7",
#       :image=>"products/product.jpg"
#     }
#   end
# 
#   # prepare Iodine::Mustache reduced Mustache template engine
#   mus_view = Iodine::Mustache.new(template: template)
# 
#   # prepare official Mustache template engine
#   view = Mustache.new
#   view.template = template
#   view.render # Call render once so the template will be compiled
# 
#   # benchmark different use cases
#   Benchmark.ips do |x|
#     x.report("Ruby Mustache render list of 1000") do |times|
#       view.render(data_1000)
#     end
#     x.report("Iodine::Mustache render list of 1000") do |times|
#       mus_view.render(data_1000)
#     end
# 
#     x.report("Ruby Mustache render list of 1000 with escaped data") do |times|
#       view.render(data_1000_escaped)
#     end
#     x.report("Iodine::Mustache render list of 1000 with escaped data") do |times|
#       mus_view.render(data_1000_escaped)
#     end
# 
#     x.report("Ruby Mustache - no caching - render list of 1000") do |times|
#       Mustache.render(template, data_1000)
#     end
#     x.report("Iodine::Mustache - no caching - render list of 1000") do |times|
#       Iodine::Mustache.render(nil, template, data_1000)
#     end
#     x.compare!
#   end ; nil
# end
# 
# benchmark_mustache
# ```
# 
  class Mustache
  end
  # These are some utility, unescaping and decoding helpers provided by Iodine.
  # 
  # These **should** be faster then their common Ruby / Rack alternative, but this may depend on your own machine and Ruby version.
  # 
  # ```ruby
  # require 'iodine'
  # require 'rack'
  # require 'cgi'
  # require 'benchmark/ips'
  # # a String in need of decoding
  # encoded = '%E3 + %83 + %AB + %E3 + %83 + %93 + %E3 + %82 + %A4 + %E3 + %82 + %B9 + %E3 + %81 + %A8'
  # decoded = Rack::Utils.unescape(encoded, "binary")
  # html_xss = "<script>alert('avoid xss attacks')</script>"
  # html_xss_safe = Rack::Utils.escape_html html_xss
  # short_str1 = Array.new(64) { 'a' } .join
  # short_str2 = Array.new(64) { 'a' } .join
  # long_str1 = Array.new(4094) { 'a' } .join
  # long_str2 = Array.new(4094) { 'a' } .join
  # now_preclaculated = Time.now
  # Benchmark.ips do |bm|
  #     bm.report(" Iodine rfc2822")    { Iodine::Utils.rfc2822(now_preclaculated) }
  #     bm.report("   Rack rfc2822")    {   Rack::Utils.rfc2822(now_preclaculated) }
  #   bm.compare!
  # end; Benchmark.ips do |bm|
  #     bm.report("Iodine unescape")    { Iodine::Utils.unescape encoded }
  #     bm.report("  Rack unescape")    {   Rack::Utils.unescape encoded }
  #   bm.compare!
  # end; Benchmark.ips do |bm|
  #     bm.report("Iodine escape")    { Iodine::Utils.escape decoded }
  #     bm.report("  Rack escape")    {   Rack::Utils.escape decoded }
  #   bm.compare!
  # end; Benchmark.ips do |bm|
  #     bm.report("Iodine escape HTML")    { Iodine::Utils.escape_html html_xss }
  #     bm.report("  Rack escape HTML")    {   Rack::Utils.escape_html html_xss }
  #   bm.compare!
  # end; Benchmark.ips do |bm|
  #     bm.report("Iodine unescape HTML")    { Iodine::Utils.unescape_html html_xss_safe }
  #     bm.report("   CGI unescape HTML")    {   CGI.unescapeHTML html_xss_safe }
  #   bm.compare!
  # end; Benchmark.ips do |bm|
  #     bm.report("Iodine secure compare (short)")    { Iodine::Utils.secure_compare short_str1, short_str2 }
  #     bm.report("  Rack secure compare (short)")    {   Rack::Utils.secure_compare short_str1, short_str2 }
  #   bm.compare!
  # end; Benchmark.ips do |bm|
  #     bm.report("Iodine secure compare (long)")    { Iodine::Utils.secure_compare long_str1, long_str2 }
  #     bm.report("  Rack secure compare (long)")    {   Rack::Utils.secure_compare long_str1, long_str2 }
  #   bm.compare!
  # end && nil
  # ```
  # 
  module Utils
  end
  class Error < StandardError; end
  # Your code goes here...
end
