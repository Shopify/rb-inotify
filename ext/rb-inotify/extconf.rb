require 'mkmf'

have_header("sys/inotify.h")
append_cflags([
  "-fvisibility=hidden",
  "-I..",
  "-Wall",
  "-O3",
  "-std=gnu99"
])
append_cflags(RbConfig::CONFIG["debugflags"]) if RbConfig::CONFIG["debugflags"]

create_makefile('rb-inotify/rb_inotify')
