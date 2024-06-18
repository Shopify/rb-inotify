require 'bundler'
Bundler::GemHelper.install_tasks

require 'rake/extensiontask'

Rake::ExtensionTask.new("rb_inotify") do |ext|
  ext.ext_dir = "ext/rb-inotify"
  ext.lib_dir = "lib/rb-inotify"
end

task default: :compile
