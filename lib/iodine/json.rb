module Iodine
  # Iodine includes a lenient JSON parser that attempts to ignore JSON errors when possible and adds some extensions such as Hex numerical representations and comments.
  #
  # On my system, the Iodine JSON parser is more than 40% faster than the native Ruby parser. When using symbols the speed increase is even higher.
  #
  # It's easy to monkey-patch the system's `JSON.parse` method (not the `JSON.parse!` method) by using `Iodine.patch_json`.
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
  #      JSON.parse!(STR) == Iodine::JSON.parse!(STR) # => undefined, maybe true maybe false
  #
  #      # warm-up
  #      TIMES.times { JSON.parse STR }
  #      TIMES.times { Iodine::JSON.parse STR }
  #
  #      puts ""; Benchmark.bm do |b|
  #        sys = b.report("system") { TIMES.times { JSON.parse STR } }
  #        sys_sym = b.report("system-sym") { TIMES.times { JSON.parse STR, symbolize_names: true } }
  #        iodine = b.report("iodine") { TIMES.times { Iodine::JSON.parse STR } }
  #        iodine_sym = b.report("iodine-sym") { TIMES.times { Iodine::JSON.parse STR, symbolize_names: true } }
  #
  #        puts "----------------------------"
  #        puts "Iodine::JSON speed as percent of Ruby's native JSON:"
  #        puts "normal:    #{sys/iodine}"
  #        puts "symolized: #{sys_sym/iodine_sym}"
  #      end; nil
  #
  # Note that the bang(!) method should NOT be used for monkey-patching the default JSON parser, since some important features are unsupported by the Iodine parser.
  #
  module JSON
  end
end
