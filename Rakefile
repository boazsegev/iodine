require "bundler/gem_tasks"
require "rake/extensiontask"

begin
  require 'rspec/core/rake_task'
  RSpec::Core::RakeTask.new(:spec)
rescue LoadError
end

task :default => [:compile, :spec]

Rake::ExtensionTask.new "iodine" do |ext|
  ext.lib_dir = "lib/iodine"
end

# Rake::ExtensionTask.new "iodine_http" do |ext|
#   ext.name = 'iodine_http'
#   ext.lib_dir = "lib/iodine"
#   ext.ext_dir = 'ext/iodine'
#   ext.config_script = 'extconf-http.rb'
# end
