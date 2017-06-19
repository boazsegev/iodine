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
      #       s = '%E3%83%AB%E3%83%93%E3%82%A4%E3%82%B9%E3%81%A8'
      #       Benchmark.bm do |bm|
      #         # Pre-Patch
      #         bm.report("Rack")    {1_000_000.times { Rack::Utils.unescape s } }
      #
      #         # Perform Patch
      #         Rack::Utils.class_eval do
      #           Iodine::Base::MonkeyPatch::RackUtils.methods(false).each do |m|
      #             define_singleton_method(m,
      #                   Iodine::Base::MonkeyPatch::RackUtils.instance_method(m) )
      #           end
      #         end
      #
      #         # Post Patch
      #         bm.report("Patched") {1_000_000.times { Rack::Utils.unescape s } }
      #       end && nil
      #
      # Results:
      #         user     system      total        real
      #       Rack     8.620000   0.020000   8.640000 (  8.636676)
      #       Patched  0.320000   0.000000   0.320000 (  0.322377)
      module RackUtils
      end
    end
  end
end
\
