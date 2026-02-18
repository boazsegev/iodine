# frozen_string_literal: true

require "bundler/gem_tasks"
task default: %i[]

require "rake/extensiontask"

Rake::ExtensionTask.new "iodine" do |ext|
  ext.lib_dir = "lib/iodine"
end

require "rspec/core/rake_task"
RSpec::Core::RakeTask.new(:spec) do |t|
  t.pattern = "spec/**/*_spec.rb"
  t.rspec_opts = "--format progress --no-color"
end
