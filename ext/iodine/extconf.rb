require "mkmf"

abort "Missing a Linux/Unix OS evented API (epoll/kqueue)." unless have_func("kevent") || have_func("epoll_ctl")

$CFLAGS = '-std=c11 -O3 -Wno-error=shorten-64-to-32 -Wall'

create_makefile "iodine/iodine"
