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

Performance testing requires the main Ruby `mustache` gem as well as the `benchmark-ips` gem.

Test using:

```ruby
require 'iodine/benchmark'
Iodine::Benchmark.mustache
```

On my machine this benchmark indicates about an x10 performance boost.

## Contributing

Bug reports and pull requests are welcome on GitHub at https://github.com/boazsegev/iodine.

This gem is a C extension, so you will need to know C to use it.

The gem also leverages the facil.io C STL library under the hood, so things related to the facil.io library will have to go in the facil.io repo.

Good luck!

## License

The gem is available as open source under the terms of the [MIT License](https://opensource.org/licenses/MIT).
