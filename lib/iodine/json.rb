module Iodine
  # Iodine includes a lenient JSON parser that attempts to ignore JSON errors when possible and adds some extensions such as Hex numerical representations and comments.
  #
  # On my system, the Iodine JSON parser is more than 30% faster. When using the {Iodine::JSON.parse!} method, the speed increase can be higher (I had ~63% speed increase when benchmarking).
  #
  # You can benchmark the Iodine JSON performance and decide if you wish to monkey-patch the Ruby implementation.
  #
  #      JSON_FILENAME="foo.json"
  #
  #      require 'json'
  #      require 'iodine'
  #      TIMES = 100
  #      STR = IO.binread(JSON_FILENAME); nil
  #
  #      JSON.parse(STR) == Iodine::JSON.parse(STR) # => true
  #      JSON.parse!(STR) == Iodine::JSON.parse!(STR) # => false (symbols)
  #
  #      # warm-up
  #      TIMES.times { JSON.parse STR }
  #      TIMES.times { Iodine::JSON.parse STR }
  #
  #      Benchmark.bm do |b|
  #        sys = b.report("system") { TIMES.times { JSON.parse STR } }
  #        sys_bang = b.report("system!") { TIMES.times { JSON.parse! STR } }
  #        iodine = b.report("iodine") { TIMES.times { Iodine::JSON.parse STR } }
  #        iodine_bang = b.report("iodine!") { TIMES.times { Iodine::JSON.parse! STR } }
  #
  #        puts "----------------------------"
  #        puts "Iodine::JSON speed as percent of Ruby's native JSON:"
  #        puts "normal: #{sys/iodine}"
  #        puts "bang: #{sys_bang/iodine_bang}"
  #      end
  #
  # Note that the bang(!) method behaves slightly different than the Ruby implementation, by using Symbols for Hash keys (instead of Strings).
  #
  module JSON
  end
end
