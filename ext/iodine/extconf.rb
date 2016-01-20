require "mkmf"

abort "no evented libarary support" unless have_func("kevent") || have_func("epoll_ctl")

create_makefile "iodine/iodine"
