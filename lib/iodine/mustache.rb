module Iodine
  # Iodine includes a strict and extra safe Mustache templating engine.
  #
  # The Iodine Mustache templating engine provides increased XSS protection through agressive HTML escaping. It's also faster than the original (Ruby based) Mustache templating engine.
  #
  # Another difference is that the Iodine Mustache templating engine makes it eady to load the templates from the disk (or specify a virtual filename), allowing for easy patial template path resolution.
  #
  # There's no monkey-patch for `mustache` Ruby gem since the API is incompatible.
  #
  # You can benchmark the Iodine Mustache performance and decide if you wish to switch from the Ruby implementation.
  #
  #     require 'benchmark/ips'
  #     require 'mustache'
  #     require 'iodine'
  #
  #     def benchmark_mustache
  #       # benchmark code was copied, in part, from:
  #       #   https://github.com/mustache/mustache/blob/master/benchmarks/render_collection_benchmark.rb
  #       template = """
  #       {{#products}}
  #         <div class='product_brick'>
  #           <div class='container'>
  #             <div class='element'>
  #               <img src='images/{{image}}' class='product_miniature' />
  #             </div>
  #             <div class='element description'>
  #               <a href={{url}} class='product_name block bold'>
  #                 {{external_index}}
  #               </a>
  #             </div>
  #           </div>
  #         </div>
  #       {{/products}}
  #       """
  #
  #       IO.write "test_template.mustache", template
  #       filename = "test_template.mustache"
  #
  #       data_1 = {
  #         products: [ {
  #           :external_index=>"This <product> should've been \"properly\" escaped.",
  #           :url=>"/products/7",
  #           :image=>"products/product.jpg"
  #         } ]
  #       }
  #       data_1000 = {
  #         products: []
  #       }
  #
  #       1000.times do
  #         data_1000[:products] << {
  #           :external_index=>"product",
  #           :url=>"/products/7",
  #           :image=>"products/product.jpg"
  #         }
  #       end
  #
  #       data_1000_escaped = {
  #         products: []
  #       }
  #
  #       1000.times do
  #         data_1000_escaped[:products] << {
  #           :external_index=>"This <product> should've been \"properly\" escaped.",
  #           :url=>"/products/7",
  #           :image=>"products/product.jpg"
  #         }
  #       end
  #
  #       view = Mustache.new
  #       view.template = template
  #       view.render # Call render once so the template will be compiled
  #       iodine_view = Iodine::Mustache.new(filename)
  #
  #       puts "Ruby Mustache rendering (and HTML escaping) results in:",
  #            view.render(data_1), "",
  #            "Notice that Iodine::Mustache rendering (and HTML escaping) results in agressive escaping:",
  #            iodine_view.render(data_1), "", "----"
  #
  #       # return;
  #
  #       Benchmark.ips do |x|
  #         x.report("Ruby Mustache render list of 1000") do |times|
  #           view.render(data_1000)
  #         end
  #         x.report("Iodine::Mustache render list of 1000") do |times|
  #           iodine_view.render(data_1000)
  #         end
  #
  #         x.report("Ruby Mustache render list of 1000 with escaped data") do |times|
  #           view.render(data_1000_escaped)
  #         end
  #         x.report("Iodine::Mustache render list of 1000 with escaped data") do |times|
  #           iodine_view.render(data_1000_escaped)
  #         end
  #
  #         x.report("Ruby Mustache - no caching - render list of 1000") do |times|
  #           tmp = Mustache.new
  #           tmp.template = template
  #           tmp.render(data_1000)
  #         end
  #         x.report("Iodine::Mustache - no caching - render list of 1000") do |times|
  #           Iodine::Mustache.render(nil, data_1000, template)
  #         end
  #       end
  #       nil
  #     end
  #
  #     benchmark_mustache
  class Mustache
  end
end
