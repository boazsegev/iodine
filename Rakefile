require "bundler/gem_tasks"
require "rake/extensiontask"
require_relative 'lib/iodine/version'

begin
  require 'rspec/core/rake_task'
  RSpec::Core::RakeTask.new(:rspec)
rescue LoadError
end

task :spec => [:compile, :rspec]

task :push_packages do
  Rake::Task['push_packages_to_rubygems'].invoke
  Rake::Task['push_packages_to_github'].invoke
end

task :push_packages_to_rubygems do
  system("gem push isomorfeus-iodine-#{Iodine::VERSION}.gem")
end

task :push_packages_to_github do
  system("gem push --key github --host https://rubygems.pkg.github.com/isomorfeus isomorfeus-iodine-#{Iodine::VERSION}.gem")
end

task :push do
  system("git push github")
  system("git push gitlab")
  system("git push bitbucket")
  system("git push gitprep")
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
