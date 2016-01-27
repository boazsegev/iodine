require "mkmf"

abort "Missing a Linux/Unix OS evented API (epoll/kqueue)." unless have_func("kevent") || have_func("epoll_ctl")

$CFLAGS = '-std=c11 -O3 -Wall'

create_makefile "iodine/iodine_http"
