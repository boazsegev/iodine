require "mkmf"

dir_config("openssl")

if have_library('crypto') && have_library('ssl')
  begin
    require 'openssl'
    if(OpenSSL::VERSION.to_i > 2)
      puts "* Detected OpenSSL version > 3 (#{OpenSSL::VERSION}), setting the HAVE_OPENSSL flag."
      $defs << "-DHAVE_OPENSSL"
    end
  rescue LoadError
  end
end

$defs << "-DDEBUG" if ENV["DEBUG"]

append_cflags("-Wno-undef");
append_cflags("-Wno-missing-noreturn");

create_makefile "iodine/iodine"
