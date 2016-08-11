# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'iodine/version'

Gem::Specification.new do |spec|
  spec.name          = 'iodine'
  spec.version       = Iodine::VERSION
  spec.authors       = ['Boaz Segev']
  spec.email         = ['Boaz@2be.co.il']

  spec.summary       = ' Iodine - writing C servers in Ruby.'
  spec.description   = ' Iodine - writing C servers in Ruby.'
  spec.homepage      = 'https://github.com/boazsegev/iodine'
  spec.license       = 'MIT'

  # Prevent pushing this gem to RubyGems.org by setting 'allowed_push_host', or
  # delete this section to allow pushing this gem to any host.
  if spec.respond_to?(:metadata)
    spec.metadata['allowed_push_host'] = 'none' # "https://rubygems.org"
  else
    raise 'RubyGems 2.0 or newer is required to protect against public gem pushes.'
  end

  spec.files         = `git ls-files -z`.split("\x0").reject { |f| f.match(%r{^(test|spec|features)/}) }
  spec.bindir        = 'exe'
  spec.executables   = spec.files.grep(%r{^exe/}) { |f| File.basename(f) }
  spec.require_paths = %w(lib ext)

  spec.extensions = %w(ext/iodine/extconf.rb)

  spec.required_ruby_version = '>= 2.2.2' # for Rack

  spec.add_dependency 'rack'
  spec.add_dependency 'rake-compiler'

  spec.requirements << 'A Unix based system, i.e.: Linux / OS X / BSD.'
  spec.requirements << 'An updated C compiler, with support for C11 (i.e. gcc 4.9 or later).'
  spec.requirements << 'Ruby >= 2.2.2'

  spec.add_development_dependency 'bundler', '~> 1.10'
  spec.add_development_dependency 'rake', '~> 10.0'
  spec.add_development_dependency 'minitest'

  spec.post_install_message = "** WARNING!\n" \
                              "Iodine 0.2.0 is NOT an upgrade - it's a total rewrite, it's written in C specifically for Ruby MRI.\n\n" \
                              'If your application was using Iodine 0.1.x, it might not work after this "upgrade".'
end
