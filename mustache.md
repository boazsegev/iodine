# Iodine::Mustashe

Iodine::Mustache is a lighter implementation of the mustache template rendering gem, with a focus on a few minor security details:

1. HTML escaping is more aggressive, increasing XSS protection. Read why at: [wonko.com/post/html-escaping](https://wonko.com/post/html-escaping).

2. Dot notation is tested in whole as well as in part (i.e. `user.name.first` will be tested as is, than the couplet `user`, `name.first` and than as each `user`, `name` , `first`), allowing for the Hash data to contain keys with dots while still supporting dot notation shortcuts.

3. Less logic: i.e., lambdas / procs do not automatically invoke a re-rendering... I'd remove them completely as unsafe, but for now there's that.

4. Improved Protection against Endless Recursion: i.e., Partial templates reference themselves when recursively nested (instead of being recursively re-loaded); and Partial's context is limited to their starting point's context (cannot access parent context).

It wasn't designed specifically for speed or performance... but it ended up being significantly faster.

## Usage

This approach to Mustache templates may require more forethought when designing either the template or the context's data format, however it should force implementations to be more secure and performance aware.

Approach:

```ruby
require 'iodine'
# One-off rendering of (possibly dynamic) template:
result = Iodine::Mustache.render(template: "{{foo}}", ctx: {foo: "bar"}) # => "bar"
# caching of parsed template data for multiple render operations:
view = Iodine::Mustache.new(file: "./views/foo.mustache", template: "{{foo}}")
results = Array.new(100) {|i| view.render(foo: "bar#{i}") } # => ["bar0", "bar1", ...]
```

## Performance

Performance on my machine seems very fast

```ruby
require 'benchmark/ips'
require 'mustache'
require 'iodine'

# Benchmark code was copied, in part, from:
#   https://github.com/mustache/mustache/blob/master/benchmarks/render_collection_benchmark.rb
# The test is, sadly, biased and doesn't test for missing elements, proc/method resolution or template partials.
def benchmark_mustache
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
  data_1000 = {
    products: []
  }
  data_1000_escaped = {
    products: []
  }

  1000.times do
    data_1000[:products] << {
      :external_index=>"product",
      :url=>"/products/7",
      :image=>"products/product.jpg"
    }
    data_1000_escaped[:products] << {
      :external_index=>"This <product> should've been \"properly\" escaped.",
      :url=>"/products/7",
      :image=>"products/product.jpg"
    }
  end

  # prepare Iodine::Mustache reduced Mustache template engine
  mus_view = Iodine::Mustache.new(template: template)

  # prepare official Mustache template engine
  view = Mustache.new
  view.template = template
  view.render # Call render once so the template will be compiled

  # benchmark different use cases
  Benchmark.ips do |x|
    x.report("Ruby Mustache render list of 1000") do |times|
      view.render(data_1000)
    end
    x.report("Iodine::Mustache render list of 1000") do |times|
      mus_view.render(data_1000)
    end

    x.report("Ruby Mustache render list of 1000 with escaped data") do |times|
      view.render(data_1000_escaped)
    end
    x.report("Iodine::Mustache render list of 1000 with escaped data") do |times|
      mus_view.render(data_1000_escaped)
    end

    x.report("Ruby Mustache - no caching - render list of 1000") do |times|
      tmp = Mustache.new
      tmp.template = template
      tmp.render(data_1000)
    end
    x.report("Iodine::Mustache - no caching - render list of 1000") do |times|
      Iodine::Mustache.render(nil, template, data_1000)
    end
    x.compare!
  end ; nil
end

benchmark_mustache
```

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/Iodine::Mustache.

This gem is a C extension, so you will need to know C to use it.

The gem also leverages the facil.io C STL library under the hood, so things related to the facil.io library will have to go in the facil.io repo.

Good luck!

## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).
