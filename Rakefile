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

Rake::ExtensionTask.new "iodine_http" do |ext|
  ext.name = 'iodine_http'
  ext.lib_dir = "lib/iodine"
  ext.ext_dir = 'ext/iodine'
  ext.config_script = 'extconf-http.rb'
end
