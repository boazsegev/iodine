require 'mkmf'
require 'fileutils'

# Test polling
def iodine_test_polling_support
  iodine_poll_test_kqueue = <<EOS
\#define _GNU_SOURCE
\#include <stdlib.h>
\#include <sys/event.h>
int main(void) {
  int fd = kqueue();
}
EOS

  iodine_poll_test_epoll = <<EOS
\#define _GNU_SOURCE
\#include <stdlib.h>
\#include <stdio.h>
\#include <sys/types.h>
\#include <sys/stat.h>
\#include <fcntl.h>
\#include <sys/epoll.h>
int main(void) {
  int fd = epoll_create1(EPOLL_CLOEXEC);
}
EOS

  iodine_poll_test_poll = <<EOS
\#define _GNU_SOURCE
\#include <stdlib.h>
\#include <poll.h>
int main(void) {
  struct pollfd plist[18];
  memset(plist, 0, sizeof(plist[0]) * 18);
  poll(plist, 1, 1);
}
EOS

  # Test for manual selection and then TRY_COMPILE with each polling engine
  if Gem.win_platform?
    puts "skipping polling tests, using WSAPOLL on Windows"
    $defs << "-DFIO_ENGINE_WSAPOLL"
  elsif ENV['FIO_POLL']
    puts "skipping polling tests, enforcing manual selection of: poll"
    $defs << "-DFIO_ENGINE_POLL"
  elsif ENV['FIO_FORCE_POLL']
    puts "skipping polling tests, enforcing manual selection of: poll"
    $defs << "-DFIO_ENGINE_POLL"
  elsif ENV['FIO_FORCE_EPOLL']
    puts "skipping polling tests, enforcing manual selection of: epoll"
    $defs << "-DFIO_ENGINE_EPOLL"
  elsif ENV['FIO_FORCE_KQUEUE']
    puts "* Skipping polling tests, enforcing manual selection of: kqueue"
    $defs << "-DFIO_ENGINE_KQUEUE"
  elsif try_compile(iodine_poll_test_epoll)
    puts "detected `epoll`"
    $defs << "-DFIO_ENGINE_EPOLL"
  elsif try_compile(iodine_poll_test_kqueue)
    puts "detected `kqueue`"
    $defs << "-DFIO_ENGINE_KQUEUE"
  elsif try_compile(iodine_poll_test_poll)
    puts "detected `poll` - this is suboptimal fallback!"
    $defs << "-DFIO_ENGINE_POLL"
  else
    puts "* WARNING: No supported polling engine! expecting compilation to fail."
  end
end

iodine_test_polling_support()

unless Gem.win_platform?
  # Test for OpenSSL version equal to 1.0.0 or greater.
  unless ENV['NO_SSL'] || ENV['NO_TLS'] || ENV["DISABLE_SSL"]
    OPENSSL_TEST_CODE = <<EOS
\#include <openssl/bio.h>
\#include <openssl/err.h>
\#include <openssl/ssl.h>
\#if OPENSSL_VERSION_NUMBER < 0x10100000L
\#error "OpenSSL version too small"
\#endif
int main(void) {
  SSL_library_init();
  SSL_CTX *ctx = SSL_CTX_new(TLS_method());
  SSL *ssl = SSL_new(ctx);
  BIO *bio = BIO_new_socket(3, 0);
  BIO_up_ref(bio);
  SSL_set0_rbio(ssl, bio);
  SSL_set0_wbio(ssl, bio);
}
EOS

    dir_config("openssl")
    begin
      require 'openssl'
    rescue LoadError
    else
      if have_library('crypto') && have_library('ssl')
        puts "detected OpenSSL library, testing for version and required functions."
        if try_compile(OPENSSL_TEST_CODE)
          $defs << "-DHAVE_OPENSSL"
          puts "confirmed OpenSSL to be version 1.1.0 or above (#{OpenSSL::OPENSSL_LIBRARY_VERSION})...\n* compiling with HAVE_OPENSSL."
        else
          puts "FAILED: OpenSSL version not supported (#{OpenSSL::OPENSSL_LIBRARY_VERSION} is too old)."
        end
      end
    end
  end
end

create_makefile 'iodine/iodine'
