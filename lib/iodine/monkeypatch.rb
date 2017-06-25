module Iodine
  module Base
    # Iodine does NOT monkey patch automatically at this time.
    #
    # This may change in future releases, but that's unlikely.
    module MonkeyPatch
      # Iodine does NOT monkey patch Rack automatically. However, it's possible to
      # moneky patch Rack::Utils using this module.
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
      #         user     system      total        real
      #         Rack.unescape  8.660000   0.010000   8.670000 (  8.687807)
      #         Rack.rfc2822  3.730000   0.000000   3.730000 (  3.727732)
      #         Rack.rfc2109  3.020000   0.010000   3.030000 (  3.031940)
      #                      --- Monkey Patching Rack ---
      #         Patched.unescape  0.340000   0.000000   0.340000 (  0.341506)
      #         Patched.rfc2822  0.740000   0.000000   0.740000 (  0.737796)
      #         Patched.rfc2109  0.690000   0.010000   0.700000 (  0.700155)
      #
      module RackUtils
      end
    end
  end
end
\
