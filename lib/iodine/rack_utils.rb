module Iodine
  # Iodine's {Iodine::Rack} module provides a Rack complient interface (connecting Iodine to Rack) for an HTTP and Websocket Server.
	module Rack
		# The methods in this module are designed to be Rack compatible and to provide higher performance than the original Rack methods.
		#
		# Iodine does NOT monkey patch Rack automatically. However, it's possible and recommended to moneky patch Rack::Utils to use the methods in this module.
    #
		# Choosing to monkey patch Rack::Utils could offer significant performance gains for some applications. i.e. (on my machine):
		#
		#       require 'iodine'
		#       require 'rack'
		#       # a String in need of decoding
		#       s = '%E3%83%AB%E3%83%93%E3%82%A4%E3%82%B9%E3%81%A8'
		#       Benchmark.bm do |bm|
		#         # Pre-Patch
		#         bm.report("   Rack.unescape")    {1_000_000.times { Rack::Utils.unescape s } }
		#         bm.report("    Rack.rfc2822")    {1_000_000.times { Rack::Utils.rfc2822(Time.now) } }
		#         bm.report("    Rack.rfc2109")    {1_000_000.times { Rack::Utils.rfc2109(Time.now) } }
		#         # Perform Patch
		#         Iodine.patch_rack
		#         puts "            --- Monkey Patching Rack ---"
		#         # Post Patch
		#         bm.report("Patched.unescape")    {1_000_000.times { Rack::Utils.unescape s } }
		#         bm.report(" Patched.rfc2822")    {1_000_000.times { Rack::Utils.rfc2822(Time.now) } }
		#         bm.report(" Patched.rfc2109")    {1_000_000.times { Rack::Utils.rfc2109(Time.now) } }
		#       end && nil
		#
		# Results:
		#
		#         user     system      total        real
		#         Rack.unescape  8.706881   0.019995   8.726876 (  8.740530)
		#         Rack.rfc2822  3.270305   0.007519   3.277824 (  3.279416)
		#         Rack.rfc2109  3.152188   0.003852   3.156040 (  3.157975)
		#                    --- Monkey Patching Rack ---
		#         Patched.unescape  0.327231   0.003125   0.330356 (  0.337090)
		#         Patched.rfc2822  0.691304   0.003330   0.694634 (  0.701172)
		#         Patched.rfc2109  0.685029   0.001956   0.686985 (  0.687607)
		#
		# Iodine uses the same code internally for HTTP timestamping (adding missing `Date` headers) and logging.
		module Utils
		end
	end
end
