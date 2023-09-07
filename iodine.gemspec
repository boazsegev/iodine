# frozen_string_literal: true

require_relative "lib/iodine/version"

Gem::Specification.new do |spec|
  spec.name = "iodine"
  spec.version = Iodine::VERSION
  spec.authors = ["Boaz Segev"]
  spec.email = ["bo@bowild.com"]

  spec.summary = 'iodine - a fast HTTP / Websocket Server with Pub/Sub support, optimized for Ruby MRI on Linux / BSD / Windows'
  spec.description = 'A fast HTTP / Websocket Server with built-in Pub/Sub support (with or without Redis), static file support and many other features, optimized for Ruby MRI on Linux / BSD / macOS / Windows'
  spec.homepage = "https://github.com/boazsegev/iodine"
  spec.license = "MIT"
  spec.required_ruby_version = ">= 3.0.0"

  spec.metadata["allowed_push_host"] = "https://rubygems.org"

  spec.metadata["homepage_uri"] = spec.homepage
  spec.metadata["source_code_uri"] = "https://github.com/boazsegev/iodine"
  spec.metadata["changelog_uri"] = "https://github.com/boazsegev/iodine/CHANGELOG.md"

  # Specify which files should be added to the gem when it is released.
  # The `git ls-files -z` loads the files in the RubyGem that have been added into git.
  spec.files = Dir.chdir(__dir__) do
    `git ls-files -z`.split("\x0").reject do |f|
      (f == __FILE__) || f.match(%r{\A(?:(?:bin|test|spec|features)/|\.(?:git|travis|circleci)|appveyor)})
    end
  end
  spec.bindir = "exe"
  spec.executables = spec.files.grep(%r{\Aexe/}) { |f| File.basename(f) }
  spec.require_paths = ["lib", "ext"]
  
  # Development dependencies
  spec.add_development_dependency 'rake-compiler', '>= 1', '< 2.0'

  spec.extensions = %w(ext/iodine/extconf.rb)
  spec.post_install_message = "Thank you for installing Iodine #{Iodine::VERSION}.\n" +
                              "Remember: if iodine supports your business, it's only fair to give value back (code contributions / donations)."

  # Uncomment to register a new dependency of your gem
  # spec.add_dependency "example-gem", "~> 1.0"

  # For more information and examples about making a new gem, check out our
  # guide at: https://bundler.io/guides/creating_gem.html
end
