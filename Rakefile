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
  t.rspec_opts = "--format progress --no-color --backtrace"
end

# Builds the diagnostic `iodine-test` extension (ext/iodine-test), which
# reproduces the Windows `win32_mutex_lock: WAIT_ABANDONED` teardown crash
# with zero iodine / facil.io code.
#
# Windows-only: the crash it diagnoses is Windows-specific, and the task is a
# deliberate no-op on other platforms so POSIX CI / builds are unaffected.
# It is NOT part of `compile` or `spec` - invoke explicitly or from CI.
desc "Build the diagnostic iodine-test extension (Windows CI only)"
task "compile:test" do
  unless Gem.win_platform?
    puts "compile:test is a Windows-only diagnostic - skipping."
    next
  end
  Dir.chdir("ext/iodine-test") do
    ruby "extconf.rb"
    sh "make"
  end
end
