require 'mkmf'

$CFLAGS = '-std=c11 -O3 -Wall'

if ENV['CC']
  ENV['CPP'] ||= ENV['CC']
  puts "detected user prefered compiler (#{ENV['CC']})."
elsif find_executable('clang')
  $CC = ENV['CC'] = 'clang'
  $CPP = ENV['CPP'] = 'clang'
  puts 'detected clang compiler.'
elsif find_executable('gcc-5')
  $CC = ENV['CC'] = 'gcc-5'
  $CPP = ENV['CPP'] = 'gcc-5'
  puts 'detected gcc-5 compiler.'
elsif find_executable('gcc-4.9')
  $CC = ENV['CC'] = 'gcc-4.9'
  $CPP = ENV['CPP'] = 'gcc-4.9'
  puts 'detected gcc-4.9 compiler.'
end

RbConfig::MAKEFILE_CONFIG['CC'] = $CC = ENV['CC'] if ENV['CC']
RbConfig::MAKEFILE_CONFIG['CPP'] = $CPP = ENV['CPP'] if ENV['CPP']

abort 'Missing a Linux/Unix OS evented API (epoll/kqueue).' unless have_func('kevent') || have_func('epoll_ctl')
# abort 'Missing support for atomic operations (support for C11) - is your compiler updated?' unless have_header('stdatomic.h')
# abort "Missing OpenSSL." unless have_library("ssl")

create_makefile 'iodine/iodine'
