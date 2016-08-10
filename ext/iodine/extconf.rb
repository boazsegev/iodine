require 'mkmf'

abort 'Missing a Linux/Unix OS evented API (epoll/kqueue).' unless have_func('kevent') || have_func('epoll_ctl')
abort 'Missing support for atomic operations (support for C11) - is your compiler updated?' unless have_header('stdatomic.h')
# abort "Missing OpenSSL." unless have_library("ssl")

$CFLAGS = '-std=c11 -O3 -Wall'

if find_executable('gcc')
  $CC = ENV['CC'] ||= 'gcc'
  # $CPP = ENV['CPP'] ||= 'gcc'
elsif find_executable('clang')
  $CC = ENV['CC'] ||= 'clang'
  # $CPP = ENV['CPP'] ||= 'clang'
end

$CC = ENV['CC'] if ENV['CC']
$CC = ENV['CPP'] if ENV['CPP']

create_makefile 'iodine/iodine'
