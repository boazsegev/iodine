require "mkmf"

begin
  dir_config("openssl")
  if have_library('crypto') && have_library('ssl')
    require 'openssl'
    if(OpenSSL::VERSION.to_i > 2)
      puts "* Detected OpenSSL version >= 3 (#{OpenSSL::VERSION}), setting the HAVE_OPENSSL flag."
      $defs << "-DHAVE_OPENSSL"
    else
      puts "* Detected OpenSSL with incompatible version (#{OpenSSL::VERSION})."
    end
  end
rescue => e
    puts "* Couldn't find OpenSSL - skipping!\n\t Err: #{e.message}"
end

$defs << "-DDEBUG" if ENV["DEBUG"]

append_cflags("-Wno-undef");
append_cflags("-Wno-missing-noreturn");

create_makefile "iodine/iodine"
