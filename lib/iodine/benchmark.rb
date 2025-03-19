require 'iodine'

module Iodine

  # This module includes benchmarking methods to test performance.
  # 
  # Before running the benchmarks, require the `Iodine::Benchmark` module:
  # 
  #     require 'iodine/benchmark'
  # 
  # This module does **not** include benchmarking for the Iodine server itself.
  #
  # These methods and the results shown should be used with caution, as they often tell a partial story.
  # 
  # To run the benchmarks, you'll need to install the `benchmark-ips` gem.
  # 
  # To install the benchmarking gem add `benchmark-ips` to your `Gemfile` or run:
  # 
  #     gem install benchmark-ips
  #    
  module Benchmark

    # Benchmarks Iodine::JSON vs native Ruby JSON and vs Oj (if installed).
    def self.json(data_length = 1000)
      require 'oj' rescue nil
      require 'benchmark/ips'
      require 'json'
      # make a big data store with nothings
      data_1000 = []
      data_length.times do
        tmp = {f: rand() };
        tmp[:i] = (tmp[:f] * 1000000).to_i
        tmp[:str] = tmp[:i].to_s
        tmp[:escaped] = "Hello\nNew\nLines!"
        tmp[:sym] = tmp[:str].to_sym
        tmp[:ary] = []
        tmp[:ary_empty] = []
        tmp[:hash_empty] = Hash.new
        100.times {|i| tmp[:ary] << i }
        data_1000 << tmp
      end
      json_string = data_1000.to_json
      puts "-----"
      puts "Benchmark #{data_1000.length} item tree, and #{json_string.length} bytes of JSON"
      # benchmark stringification
      ::Benchmark.ips do |x|
        x.report("      Ruby obj.to_json") do |times|
          data_1000.to_json
        end
        x.report("Iodine::JSON.stringify") do |times|
          Iodine::JSON.stringify(data_1000)
        end
        if(defined?(Oj))
          x.report("               Oj.dump") do |times|
            Oj.dump(data_1000)
          end
        end
        x.compare!
      end ; nil
      # benchmark parsing
      ::Benchmark.ips do |x|
        x.report("   Ruby JSON.parse") do |times|
          JSON.parse(json_string)
        end
        x.report("Iodine::JSON.parse") do |times|
          Iodine::JSON.parse(json_string)
        end
        if(defined?(Oj))
          x.report("           Oj.load") do |times|
            Oj.load(json_string)
          end
        end
        x.compare!
      end
      nil
    end

    # Benchmarks Iodine::Mustache vs the original Mustache Ruby gem (if installed).
    # 
    # Benchmark code was copied, in part, from:
    #   https://github.com/mustache/mustache/blob/master/benchmarks/render_collection_benchmark.rb
    #   
    # The test is, sadly, biased and doesn't test for missing elements, proc/method resolution or template partials.
    # 
    def self.mustache(items = 1000)
      require 'benchmark/ips'
      require 'mustache'
      template = """
      {{#products}}
        <div class='product_brick'>
          <div class='container'>
            <div class='element'>
              <img src='images/{{image}}' class='product_miniature' />
            </div>
            <div class='element description'>
              <a href={{url}} class='product_name block bold'>
                {{external_index}}
              </a>
            </div>
          </div>
        </div>
      {{/products}}
      """
      
      # fill Hash objects with values for template rendering
      data = {
        products: []
      }
      data_escaped = {
        products: []
      }

      items.times do
        data[:products] << {
          :external_index=>"product",
          :url=>"/products/7",
          :image=>"products/product.jpg"
        }
        data_escaped[:products] << {
          :external_index=>"This <product> should've been \"properly\" escaped.",
          :url=>"/products/7",
          :image=>"products/product.jpg"
        }
      end

      # prepare Iodine::Mustache reduced Mustache template engine
      mus_view = Iodine::Mustache.new template: template

      # prepare official Mustache template engine
      view = ::Mustache.new
      view.template = template
      view.render # Call render once so the template will be compiled

      # benchmark different use cases
      ::Benchmark.ips do |x|
        x.report("Ruby Mustache render list of #{items.to_s}") do |times|
          view.render(data)
        end
        x.report("Iodine::Mustache render list of #{items.to_s}") do |times|
          mus_view.render(data)
        end

        x.report("Ruby Mustache render list of #{items.to_s} with escaped data") do |times|
          view.render(data_escaped)
        end
        x.report("Iodine::Mustache render list of #{items.to_s} with escaped data") do |times|
          mus_view.render(data_escaped)
        end

        x.report("Ruby Mustache - no caching - render list of #{items.to_s}") do |times|
          ::Mustache.render(template, data)
        end
        x.report("Iodine::Mustache - no caching - render list of #{items.to_s}") do |times|
          Iodine::Mustache.render(template: template, ctx: data)
        end
        x.compare!
      end ; nil
    end

    # Benchmark the {Iodine::Utils} module and see if you want to use {Iodine::Utils.monkey_patch} when using Rack.
    def self.utils
      require 'rack'
      require 'cgi'
      require 'benchmark/ips'
      # a String in need of decoding
      encoded = '%E3 + %83 + %AB + %E3 + %83 + %93 + %E3 + %82 + %A4 + %E3 + %82 + %B9 + %E3 + %81 + %A8'
      decoded = ::Rack::Utils.unescape(encoded, "binary")
      html_xss = "<script>alert('avoid xss attacks')</script>"
      html_xss_safe = Rack::Utils.escape_html html_xss
      short_str1 = Array.new(64) { 'a' } .join
      short_str2 = Array.new(64) { 'a' } .join
      long_str1 = Array.new(4094) { 'a' } .join
      long_str2 = Array.new(4094) { 'a' } .join
      now_preclaculated = Time.now
      ::Benchmark.ips do |bm|
          bm.report(" Iodine rfc2822")    { Iodine::Utils.rfc2822(now_preclaculated) }
          bm.report("   Rack rfc2822")    {   ::Rack::Utils.rfc2822(now_preclaculated) }
          bm.compare!
      end; ::Benchmark.ips do |bm|
          bm.report("Iodine unescape")    { Iodine::Utils.unescape encoded }
          bm.report("  Rack unescape")    {   ::Rack::Utils.unescape encoded }
          bm.compare!
      end; ::Benchmark.ips do |bm|
          bm.report("Iodine escape")    { Iodine::Utils.escape decoded }
          bm.report("  Rack escape")    {   ::Rack::Utils.escape decoded }
          bm.compare!
      end; ::Benchmark.ips do |bm|
          bm.report("Iodine escape HTML")    { Iodine::Utils.escape_html html_xss }
          bm.report("  Rack escape HTML")    {   ::Rack::Utils.escape_html html_xss }
          bm.compare!
      end; ::Benchmark.ips do |bm|
          bm.report("Iodine unescape HTML")    { Iodine::Utils.unescape_html html_xss_safe }
          bm.report("   CGI unescape HTML")    {   CGI.unescapeHTML html_xss_safe }
          bm.compare!
      end; ::Benchmark.ips do |bm|
          bm.report("Iodine secure compare (#{short_str2.bytesize} Bytes)")    { Iodine::Utils.secure_compare short_str1, short_str2 }
          bm.report("  Rack secure compare (#{short_str2.bytesize} Bytes)")    {   ::Rack::Utils.secure_compare short_str1, short_str2 }
          bm.compare!
      end; ::Benchmark.ips do |bm|
          bm.report("Iodine secure compare (#{long_str1.bytesize} Bytes)")    { Iodine::Utils.secure_compare long_str1, long_str2 }
          bm.report("  Rack secure compare (#{long_str1.bytesize} Bytes)")    {   ::Rack::Utils.secure_compare long_str1, long_str2 }
        bm.compare!
      end && nil
    end

    # Benchmark the internal mini-Hash map.
    def self.minimap(counter = 20)
      require 'benchmark/ips'
      keys = []
      (counter + 1).times {|i| keys << "counter_string_#{i}" }
      frozen_keys = []
      (counter + 1).times {|i| frozen_keys << "counter_string_#{i}".freeze }

      tests = [
          ["New (#{counter * 2} Empty Maps)", Proc.new {|m| counter.times { m.class.new } }, true ],
          ["New + Set (#{counter} Numbers)", Proc.new {|m| counter.times {|i| m[i] = i } }, true ],
          ["Overwrite (#{counter} Numbers)", Proc.new {|m| counter.times {|i| m[i] = i } } ],
          ["Get (#{counter} Numbers + 1 missing)", Proc.new {|m| (counter + 1).times {|i| m[i] } } ],
          ["New + Set (#{counter} Strings)", Proc.new {|m| counter.times {|i| m["counter_string_#{i}"] = i } }, true ],
          ["Overwrite (#{counter} Strings)", Proc.new {|m| counter.times {|i| m["counter_string_#{i}"] = i } } ],
          ["Get (#{counter} Strings + 1 missing)", Proc.new {|m| (counter + 1).times {|i| m["counter_string_#{i}"] } } ],
          ["New + Set (#{counter} Strings.freeze)", Proc.new {|m| counter.times {|i| m[frozen_keys[i]] = keys[i] } }, true ],
          ["Overwrite (#{counter} Strings.freeze)", Proc.new {|m| counter.times {|i| m[frozen_keys[i]] = keys[i] } } ],
          ["Get (#{counter} Strings.freeze + 1 missing)", Proc.new {|m| (counter + 1).times {|i| m[frozen_keys[i]] } } ],
          ["each (#{counter} Strings)", Proc.new {|m| m.each {|k,v| k; v; } } ]
        ]


      klasses = [Iodine::Base::MiniMap, ::Hash]

      maps = []
      klasses.each {|k| maps << k.new }

      tests.each do |t|
        ::Benchmark.ips do |bm|
          klasses.each_index do |i|
            if(t[2])
              bm.report("#{klasses[i].name.ljust(22)} #{t[0]}") { maps[i] = klasses[i].new ; t[1].call(maps[i]) } 
            else
              bm.report("#{klasses[i].name.ljust(22)} #{t[0]}") { t[1].call(maps[i]) }
            end
          end
          bm.compare!
        end
      end
      nil
    end

  end
end
