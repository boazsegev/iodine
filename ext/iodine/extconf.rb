require 'mkmf'

# TLS backend selection:
# - IODINE_USE_EMBEDDED_TLS=1 forces embedded TLS 1.3 as default
# - Otherwise, OpenSSL is used if available and compatible
begin
  dir_config('openssl')
  if ENV['IODINE_USE_EMBEDDED_TLS']
    puts '* Default TLS: embedded (fio_tls13) - forced by IODINE_USE_EMBEDDED_TLS'
  elsif have_library('crypto') && have_library('ssl')
    require 'openssl'
    if OpenSSL::VERSION.to_i > 2
      puts "* Detected OpenSSL version >= 3 (#{OpenSSL::VERSION}), setting the HAVE_OPENSSL flag."
      $defs << '-DHAVE_OPENSSL'
    else
      puts "* OpenSSL version incompatible (#{OpenSSL::VERSION}), using embedded TLS 1.3"
    end
  else
    puts '* OpenSSL not found, using embedded TLS 1.3'
  end
rescue StandardError => e
  puts "* Couldn't find OpenSSL - using embedded TLS 1.3\n\t Err: #{e.message}"
end

$defs << '-DDEBUG' if ENV['DEBUG']

append_cflags('-Wno-undef')
append_cflags('-Wno-missing-noreturn')

# Windows requires crypt32 for cryptographic primitives used by the TLS backend
if /mingw|mswin/ =~ RUBY_PLATFORM
  $libs = append_library($libs, 'crypt32')
end

create_makefile 'iodine/iodine'
