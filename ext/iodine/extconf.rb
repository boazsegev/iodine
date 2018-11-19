require 'mkmf'

abort 'Missing a Linux/Unix OS evented API (epoll/kqueue).' unless have_func('kevent') || have_func('epoll_ctl')

if ENV['CC']
  ENV['CPP'] ||= ENV['CC']
  puts "detected user prefered compiler (#{ENV['CC']}):", `#{ENV['CC']} -v`
elsif find_executable('clang') && puts('testing clang for stdatomic support...').nil? && system("printf \"\#include <stdatomic.h>\nint main(void) {}\" | clang -include stdatomic.h -xc -o /dev/null -", out: '/dev/null')
  $CC = ENV['CC'] = 'clang'
  $CPP = ENV['CPP'] = 'clang'
  puts "using clang compiler v. #{`clang -dumpversion`}."
elsif find_executable('gcc') && (`gcc -dumpversion 2>&1`.to_i >= 5)
  $CC = ENV['CC'] = 'gcc'
  $CPP = ENV['CPP'] = find_executable('g++') ? 'g++' : 'gcc'
  puts "using gcc #{ `gcc -dumpversion 2>&1`.to_i }"
elsif find_executable('gcc-6')
  $CC = ENV['CC'] = 'gcc-6'
  $CPP = ENV['CPP'] = find_executable('g++-6') ? 'g++-6' : 'gcc-6'
  puts 'using gcc-6 compiler.'
elsif find_executable('gcc-5')
  $CC = ENV['CC'] = 'gcc-5'
  $CPP = ENV['CPP'] = find_executable('g++-5') ? 'g++-5' : 'gcc-5'
  puts 'using gcc-5 compiler.'
elsif find_executable('gcc-4.9')
  $CC = ENV['CC'] = 'gcc-4.9'
  $CPP = ENV['CPP'] = find_executable('g++-4.9') ? 'g++-4.9' : 'gcc-4.9'
  puts 'using gcc-4.9 compiler.'
else
  puts 'using an unknown (old?) compiler... who knows if this will work out... we hope.'
end

RbConfig::MAKEFILE_CONFIG['CFLAGS'] = $CFLAGS = "#{$CFLAGS} -std=c11 -DFIO_PRINT_STATE=0 #{ENV['CFLAGS']}"
RbConfig::MAKEFILE_CONFIG['CC'] = $CC = ENV['CC'] if ENV['CC']
RbConfig::MAKEFILE_CONFIG['CPP'] = $CPP = ENV['CPP'] if ENV['CPP']
puts "CFLAGS= #{$CFLAGS}"

# abort "Missing OpenSSL." unless have_library("ssl")

create_makefile 'iodine/iodine'
