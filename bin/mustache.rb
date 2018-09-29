#!/usr/bin/env ruby

require 'benchmark/ips'
require 'mustache'
require 'iodine'

def benchmark_mustache
  # benchmark code was copied, in part, from:
  #   https://github.com/mustache/mustache/blob/master/benchmarks/render_collection_benchmark.rb
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

  IO.write "test_template.mustache", template
  filename = "test_template.mustache"

  data_1 = {
    products: [ {
      :external_index=>"This <product> should've been \"properly\" escaped.",
      :url=>"/products/7",
      :image=>"products/product.jpg"
    } ]
  }
  data_1000 = {
    products: []
  }

  1000.times do
    data_1000[:products] << {
      :external_index=>"product",
      :url=>"/products/7",
      :image=>"products/product.jpg"
    }
  end

  data_1000_escaped = {
    products: []
  }

  1000.times do
    data_1000_escaped[:products] << {
      :external_index=>"This <product> should've been \"properly\" escaped.",
      :url=>"/products/7",
      :image=>"products/product.jpg"
    }
  end

  view = Mustache.new
  view.template = template
  view.render # Call render once so the template will be compiled
  iodine_view = Iodine::Mustache.new(filename)

  puts "Ruby Mustache rendering (and HTML escaping) results in:",
       view.render(data_1), "",
       "Notice that Iodine::Mustache rendering (and HTML escaping) results in agressive escaping:",
       iodine_view.render(data_1), "", "----",
       "Uncached -- Notice that Iodine::Mustache rendering (and HTML escaping) results in agressive escaping:",
       Iodine::Mustache.render(filename, data_1), "", "----"

  # return;

  Benchmark.ips do |x|
    x.report("Ruby Mustache render list of 1000") do |times|
      view.render(data_1000)
    end
    x.report("Iodine::Mustache render list of 1000") do |times|
      iodine_view.render(data_1000)
    end

    x.report("Ruby Mustache render list of 1000 with escaped data") do |times|
      view.render(data_1000_escaped)
    end
    x.report("Iodine::Mustache render list of 1000 with escaped data") do |times|
      iodine_view.render(data_1000_escaped)
    end

    x.report("Ruby Mustache - no chaching - render list of 1000") do |times|
      tmp = Mustache.new
      tmp.template = template
      tmp.render(data_1000)
    end
    x.report("Iodine::Mustache - no chaching - render list of 1000") do |times|
      Iodine::Mustache.render(filename, data_1000)
    end

  end
end

benchmark_mustache
