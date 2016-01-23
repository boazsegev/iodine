require "bundler/gem_tasks"
require "rake/testtask"
require "rake/extensiontask"

Rake::TestTask.new(:test) do |t|
  t.libs << "test"
  t.libs << "lib"
  t.test_files = FileList['test/**/*_test.rb']
end

task :default => [:compile, :test]

Rake::ExtensionTask.new "iodine" do |ext|
  ext.lib_dir = "lib/iodine"
end
