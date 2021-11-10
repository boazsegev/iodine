# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'iodine/version'

Gem::Specification.new do |spec|
  spec.name          = 'isomorfeus-iodine'
  spec.version       = Iodine::VERSION
  spec.authors       = ['Jan Biedermann']
  spec.email         = ['jan@kursator.de']

  spec.summary       = 'iodine build for Isomorfeus'
  spec.description   = 'iodine build for Isomorfeus'
  spec.homepage      = 'https://github.com/janbiedermann/iodine'
  spec.license       = 'MIT'

  spec.files         = `git ls-files -z`.split("\x0").reject { |f| f.match(%r{^(test|spec|features)/}) }
  spec.bindir        = 'exe'
  spec.executables   = spec.files.grep(%r{^exe/}) { |f| File.basename(f) }
  spec.require_paths = %w(lib ext)

  spec.extensions = %w(ext/iodine/extconf.rb)

  spec.required_ruby_version = '>= 2.2.2' # Because earlier versions had been discontinued

  spec.requirements << 'A Unix based system: Linux / macOS / BSD.'
  spec.requirements << 'An updated C compiler.'
  spec.requirements << 'Ruby >= 2.3.8 (Ruby EOL).'
  spec.requirements << 'Ruby >= 2.5.0 recommended.'
  spec.requirements << 'TLS requires OpenSSL >= 1.1.0.'
  spec.requirements << 'Or Windows with Ruby >= 3.0.0 build with MingW and MingW as compiler.'

  # spec.add_development_dependency 'bundler', '>= 1.10', '< 2.0'
  spec.add_development_dependency 'rake', '~> 13.0', '< 14.0'
  spec.add_development_dependency 'minitest', '>=5', '< 6.0'
  spec.add_development_dependency 'rspec', '>=3.9.0', '< 4.0'
  spec.add_development_dependency 'spec', '>=5.3.0', '< 6.0'
  spec.add_development_dependency 'rake-compiler', '>= 1', '< 2.0'

  spec.post_install_message = "Thank you for installing Iodine #{Iodine::VERSION}.\n" +
                              "Remember: if iodine supports your business, it's only fair to give value back (code contributions / donations) to Bo."
end
