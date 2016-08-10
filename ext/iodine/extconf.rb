require 'mkmf'

abort 'Missing a Linux/Unix OS evented API (epoll/kqueue).' unless have_func('kevent') || have_func('epoll_ctl')
# abort "Missing OpenSSL." unless have_library("ssl")

$CFLAGS = '-std=c11 -O3 -Wall'

if find_executable('gcc')
  puts 'Attempting to set compiler to gcc.'
  $CC = ENV['CC'] ||= 'gcc'
elsif find_executable('clang')
  puts 'Attempting to set compiler to clang.'
  $CC = ENV['CC'] ||= 'clang'
end

RbConfig::MAKEFILE_CONFIG['CC'] = ENV['CC'] if ENV['CC']

create_makefile 'iodine/iodine'
