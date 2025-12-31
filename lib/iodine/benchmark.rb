require 'iodine'

module Iodine
  # This module includes benchmarking methods to test performance.
  #
  # Before running the benchmarks, require the `Iodine::Benchmark` module:
  #
  #     require 'iodine/benchmark'
  #
  # This module does **not** include benchmarking for the Iodine server itself.
  #
  # These methods and the results shown should be used with caution, as they often tell a partial story.
  #
  # To run the benchmarks, you'll need to install the `benchmark-ips` gem.
  #
  # To install the benchmarking gem add `benchmark-ips` to your `Gemfile` or run:
  #
  #     gem install benchmark-ips
  #
  module Benchmark
    # Benchmarks Iodine::JSON vs native Ruby JSON and vs Oj (if installed).
    def self.json(data_length = 1000)
      begin
        require 'oj'
      rescue LoadError
        nil
      end
      require 'benchmark/ips'
      require 'json'
      # make a big data store with nothings
      data_1000 = []
      data_length.times do
        tmp = { f: rand }
        tmp[:i] = (tmp[:f] * 1_000_000).to_i
        tmp[:str] = tmp[:i].to_s
        tmp[:escaped] = "Hello\nNew\nLines!"
        tmp[:sym] = tmp[:str].to_sym
        tmp[:ary] = []
        tmp[:ary_empty] = []
        tmp[:hash_empty] = {}
        100.times { |i| tmp[:ary] << i }
        data_1000 << tmp
      end
      json_string = data_1000.to_json
      puts '-----'
      puts "Benchmark #{data_1000.length} item tree, and #{json_string.length} bytes of JSON"
      # benchmark stringification
      ::Benchmark.ips do |x|
        x.report('      Ruby obj.to_json') do |_times|
          data_1000.to_json
        end
        x.report('::JSON.pretty_generate') do |_times|
          ::JSON.pretty_generate(data_1000)
        end
        x.report('Iodine::JSON.stringify') do |_times|
          Iodine::JSON.stringify(data_1000)
        end
        x.report('Iodine::JSON.beautify') do |_times|
          Iodine::JSON.beautify(data_1000)
        end
        if defined?(Oj)
          x.report('               Oj.dump') do |_times|
            Oj.dump(data_1000)
          end
        end
        x.compare!
      end

      # benchmark parsing
      ::Benchmark.ips do |x|
        x.report('   Ruby JSON.parse') do |_times|
          JSON.parse(json_string)
        end
        x.report('Iodine::JSON.parse') do |_times|
          Iodine::JSON.parse(json_string)
        end
        if defined?(Oj)
          x.report('           Oj.load') do |_times|
            Oj.load(json_string)
          end
        end
        x.compare!
      end
      nil
    end

    # Benchmarks Iodine::Mustache vs the original Mustache Ruby gem (if installed).
    #
    # Benchmark code was copied, in part, from:
    #   https://github.com/mustache/mustache/blob/master/benchmarks/render_collection_benchmark.rb
    #
    # The test is, sadly, biased and doesn't test for missing elements, proc/method resolution or template partials.
    #
    def self.mustache(items = 1000)
      require 'benchmark/ips'
      require 'mustache'
      template = "
      {{#products}}
        <div class='product_brick'>
          <div class='container'>
            <div class='element'>
              <img src='images/{{image}}' class='product_miniature' />
            </div>
            <div class='element description'>
              <a href={{url}} class='product_name block bold'>
                {{external_index}}
              </a>
            </div>
          </div>
        </div>
      {{/products}}
      "

      # fill Hash objects with values for template rendering
      data = {
        products: []
      }
      data_escaped = {
        products: []
      }

      items.times do
        data[:products] << {
          external_index: 'product',
          url: '/products/7',
          image: 'products/product.jpg'
        }
        data_escaped[:products] << {
          external_index: "This <product> should've been \"properly\" escaped.",
          url: '/products/7',
          image: 'products/product.jpg'
        }
      end

      # prepare Iodine::Mustache reduced Mustache template engine
      mus_view = Iodine::Mustache.new template: template

      # prepare official Mustache template engine
      view = ::Mustache.new
      view.template = template
      view.render # Call render once so the template will be compiled

      # benchmark different use cases
      ::Benchmark.ips do |x|
        x.report("Ruby Mustache render list of #{items}") do |_times|
          view.render(data)
        end
        x.report("Iodine::Mustache render list of #{items}") do |_times|
          mus_view.render(data)
        end

        x.report("Ruby Mustache render list of #{items} with escaped data") do |_times|
          view.render(data_escaped)
        end
        x.report("Iodine::Mustache render list of #{items} with escaped data") do |_times|
          mus_view.render(data_escaped)
        end

        x.report("Ruby Mustache - no caching - render list of #{items}") do |_times|
          ::Mustache.render(template, data)
        end
        x.report("Iodine::Mustache - no caching - render list of #{items}") do |_times|
          Iodine::Mustache.render(template: template, ctx: data)
        end
        x.compare!
      end
      nil
    end

    # Benchmark the {Iodine::Utils} module and see if you want to use {Iodine::Utils.monkey_patch} when using Rack.
    def self.random
      require 'benchmark/ips'
      require 'securerandom'
      begin
        require 'openssl'
      rescue LoadError
        nil
      end
      ::Benchmark.ips do |bm|
        benchmark_secret = Iodine.secret
        benchmark_payload = (File.exist?(__FILE__) ? IO.binread(__FILE__)[0...512] : 'my message payload')
        digest_sha1 = 'SHA1'
        digest_sha512 = 'SHA512'
        bm.report('         Iodine::Utils.uuid(HMAC)')    { Iodine::Utils.uuid(benchmark_secret, benchmark_payload) }
        bm.report('            Iodine::Utils.hmac128')    do
          Iodine::Utils.hmac128(benchmark_secret, benchmark_payload)
        end
        bm.report('            Iodine::Utils.hmac160') do
          Iodine::Utils.hmac160(benchmark_secret, benchmark_payload)
        end
        bm.report('            Iodine::Utils.hmac512') do
          Iodine::Utils.hmac512(benchmark_secret, benchmark_payload)
        end
        if defined?(OpenSSL)
          bm.report('  OpenSSL::HMAC.base64digest SHA1') do
            OpenSSL::HMAC.base64digest(digest_sha1, benchmark_secret, benchmark_payload)
          end
          bm.report('OpenSSL::HMAC.base64digest SHA512') do
            OpenSSL::HMAC.base64digest(digest_sha512, benchmark_secret, benchmark_payload)
          end
        end
        puts "Performing comparison of HMAC with #{benchmark_payload.bytesize} byte Message and a #{benchmark_secret.bytesize} byte Secret."
        bm.compare!
      end

      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.uuid') { Iodine::Utils.uuid }
        bm.report(' SecureRandom.uuid')    { SecureRandom.uuid }
        bm.report('    Random.uuid_v4')    { Random.uuid_v4 }
        puts "Performing comparison of random UUID generation (i.e. #{Iodine::Utils.uuid})."
        bm.compare!
      end

      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.random(16)') { Iodine::Utils.random(16) }
        bm.report('       Random.bytes(16)') { ::Random.bytes(16) }
        puts "Performing comparison of random UUID generation (i.e. #{Iodine::Utils.uuid})."
        bm.compare!
      end

      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.random(4096)') { Iodine::Utils.random(4096) }
        bm.report('        Random.bytes(4096)') { ::Random.bytes(4096) }
        bm.compare!
      end

      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.random') { Iodine::Utils.random }
        bm.report('   SecureRandom.rand')    { SecureRandom.rand }
        bm.report('         Random.rand')    { ::Random.rand }
        bm.compare!
      end
      nil
    end

    # Benchmark the {Iodine::Utils} module and see if you want to use {Iodine::Utils.monkey_patch} when using Rack.
    def self.utils
      require 'rack'
      require 'cgi'
      require 'benchmark/ips'
      # a String in need of decoding
      encoded = '%E3 + %83 + %AB + %E3 + %83 + %93 + %E3 + %82 + %A4 + %E3 + %82 + %B9 + %E3 + %81 + %A8'
      decoded = ::Rack::Utils.unescape(encoded, 'binary')
      html_xss = "<script>alert('avoid xss attacks')</script>"
      html_xss_safe = Rack::Utils.escape_html html_xss
      short_str1 = Array.new(64) { 'a' }.join
      short_str2 = Array.new(64) { 'a' }.join
      long_str1 = Array.new(4094) { 'a' }.join
      long_str2 = Array.new(4094) { 'a' }.join
      now_preclaculated = Time.now
      ::Benchmark.ips do |bm|
        bm.report(' Iodine rfc2822')    { Iodine::Utils.rfc2822(now_preclaculated) }
        bm.report('   Rack rfc2822')    { ::Rack::Utils.rfc2822(now_preclaculated) }
        bm.compare!
      end; ::Benchmark.ips do |bm|
        bm.report('Iodine unescape')    { Iodine::Utils.unescape encoded }
        bm.report('  Rack unescape')    { ::Rack::Utils.unescape encoded }
        bm.compare!
      end; ::Benchmark.ips do |bm|
        bm.report('Iodine escape') { Iodine::Utils.escape decoded }
        bm.report('  Rack escape') { ::Rack::Utils.escape decoded }
        bm.compare!
      end; ::Benchmark.ips do |bm|
        bm.report('Iodine escape HTML') { Iodine::Utils.escape_html html_xss }
        bm.report('  Rack escape HTML') { ::Rack::Utils.escape_html html_xss }
        bm.compare!
      end; ::Benchmark.ips do |bm|
        bm.report('Iodine unescape HTML')    { Iodine::Utils.unescape_html html_xss_safe }
        bm.report('   CGI unescape HTML')    { CGI.unescapeHTML html_xss_safe }
        bm.compare!
      end; ::Benchmark.ips do |bm|
        bm.report("Iodine secure compare (#{short_str2.bytesize} Bytes)") do
          Iodine::Utils.secure_compare short_str1, short_str2
        end
        bm.report("  Rack secure compare (#{short_str2.bytesize} Bytes)") do
          ::Rack::Utils.secure_compare short_str1, short_str2
        end
        bm.compare!
      end; ::Benchmark.ips do |bm|
        bm.report("Iodine secure compare (#{long_str1.bytesize} Bytes)") do
          Iodine::Utils.secure_compare long_str1, long_str2
        end
        bm.report("  Rack secure compare (#{long_str1.bytesize} Bytes)") do
          ::Rack::Utils.secure_compare long_str1, long_str2
        end
        bm.compare!
      end && nil
    end

    # Benchmark the internal mini-Hash map.
    def self.minimap(counter = 20)
      require 'benchmark/ips'
      keys = []
      (counter + 1).times { |i| keys << "counter_string_#{i}" }
      frozen_keys = []
      (counter + 1).times { |i| frozen_keys << "counter_string_#{i}".freeze }

      tests = [
        ["New (#{counter * 2} Empty Maps)", proc { |m| counter.times { m.class.new } }, true],
        ["New + Set (#{counter} Numbers)", proc { |m| counter.times { |i| m[i] = i } }, true],
        ["Overwrite (#{counter} Numbers)", proc { |m| counter.times { |i| m[i] = i } }],
        ["Get (#{counter} Numbers + 1 missing)", proc { |m| (counter + 1).times { |i| m[i] } }],
        ["New + Set (#{counter} Strings)", proc { |m| counter.times { |i| m["counter_string_#{i}"] = i } }, true],
        ["Overwrite (#{counter} Strings)", proc { |m| counter.times { |i| m["counter_string_#{i}"] = i } }],
        ["Get (#{counter} Strings + 1 missing)", proc { |m| (counter + 1).times { |i| m["counter_string_#{i}"] } }],
        ["New + Set (#{counter} Strings.freeze)", proc { |m|
          counter.times do |i|
            m[frozen_keys[i]] = keys[i]
          end
        }, true],
        ["Overwrite (#{counter} Strings.freeze)", proc { |m| counter.times { |i| m[frozen_keys[i]] = keys[i] } }],
        ["Get (#{counter} Strings.freeze + 1 missing)", proc { |m| (counter + 1).times { |i| m[frozen_keys[i]] } }],
        ["each (#{counter} Strings)", proc { |m|
          m.each do |k, v|
          end
        }]
      ]

      klasses = [Iodine::Base::MiniMap, ::Hash]

      maps = []
      klasses.each { |k| maps << k.new }

      tests.each do |t|
        ::Benchmark.ips do |bm|
          klasses.each_index do |i|
            if t[2]
              bm.report("#{klasses[i].name.ljust(22)} #{t[0]}") do
                maps[i] = klasses[i].new
                t[1].call(maps[i])
              end
            else
              bm.report("#{klasses[i].name.ljust(22)} #{t[0]}") { t[1].call(maps[i]) }
            end
          end
          bm.compare!
        end
      end
      nil
    end

    # Benchmarks Iodine's AEAD symmetric encryption ciphers vs OpenSSL equivalents.
    #
    # Tests ChaCha20-Poly1305, XChaCha20-Poly1305, AES-128-GCM, and AES-256-GCM
    # with various payload sizes (64 bytes, 1KB, 64KB).
    #
    # Requires OpenSSL for comparison benchmarks.
    def self.crypto_symmetric
      require 'benchmark/ips'
      begin
        require 'openssl'
      rescue LoadError
        nil
      end

      # Test data sizes
      sizes = {
        '64B' => 64,
        '1KB' => 1024,
        '64KB' => 65_536
      }

      # Keys and nonces
      key_32 = Iodine::Utils.random(32)
      key_16 = Iodine::Utils.random(16)
      nonce_12 = Iodine::Utils.random(12)
      nonce_24 = Iodine::Utils.random(24)
      ad = 'additional authenticated data'

      puts '=' * 70
      puts 'AEAD Symmetric Encryption Benchmarks'
      puts '=' * 70

      sizes.each do |size_name, size|
        plaintext = Iodine::Utils.random(size)

        puts "\n--- Payload Size: #{size_name} ---\n\n"

        # ChaCha20-Poly1305 benchmark
        puts 'ChaCha20-Poly1305 (32-byte key, 12-byte nonce):'
        ::Benchmark.ips do |x|
          x.report('Iodine ChaCha20-Poly1305 encrypt') do
            Iodine::Base::Crypto::ChaCha20Poly1305.encrypt(plaintext, key: key_32, nonce: nonce_12, ad: ad)
          end

          if defined?(OpenSSL) && OpenSSL::Cipher.ciphers.include?('chacha20-poly1305')
            x.report('OpenSSL ChaCha20-Poly1305 encrypt') do
              cipher = OpenSSL::Cipher.new('chacha20-poly1305')
              cipher.encrypt
              cipher.key = key_32
              cipher.iv = nonce_12
              cipher.auth_data = ad
              cipher.update(plaintext)
              cipher.final
              cipher.auth_tag
            end
          end

          x.compare!
        end

        # Decrypt benchmark
        ciphertext_chacha, mac_chacha = Iodine::Base::Crypto::ChaCha20Poly1305.encrypt(plaintext, key: key_32,
                                                                                                  nonce: nonce_12, ad: ad)

        ::Benchmark.ips do |x|
          x.report('Iodine ChaCha20-Poly1305 decrypt') do
            Iodine::Base::Crypto::ChaCha20Poly1305.decrypt(ciphertext_chacha, mac: mac_chacha, key: key_32,
                                                                              nonce: nonce_12, ad: ad)
          end

          if defined?(OpenSSL) && OpenSSL::Cipher.ciphers.include?('chacha20-poly1305')
            # Prepare OpenSSL ciphertext
            cipher = OpenSSL::Cipher.new('chacha20-poly1305')
            cipher.encrypt
            cipher.key = key_32
            cipher.iv = nonce_12
            cipher.auth_data = ad
            openssl_ciphertext = cipher.update(plaintext) + cipher.final
            openssl_tag = cipher.auth_tag

            x.report('OpenSSL ChaCha20-Poly1305 decrypt') do
              decipher = OpenSSL::Cipher.new('chacha20-poly1305')
              decipher.decrypt
              decipher.key = key_32
              decipher.iv = nonce_12
              decipher.auth_data = ad
              decipher.auth_tag = openssl_tag
              decipher.update(openssl_ciphertext) + decipher.final
            end
          end

          x.compare!
        end

        # XChaCha20-Poly1305 benchmark (Iodine only - OpenSSL doesn't support XChaCha20)
        puts "\nXChaCha20-Poly1305 (32-byte key, 24-byte nonce - safe for random nonces):"
        ::Benchmark.ips do |x|
          x.report('Iodine XChaCha20-Poly1305 encrypt') do
            Iodine::Base::Crypto::XChaCha20Poly1305.encrypt(plaintext, key: key_32, nonce: nonce_24, ad: ad)
          end
          x.compare!
        end

        # AES-128-GCM benchmark
        puts "\nAES-128-GCM (16-byte key, 12-byte nonce):"
        ::Benchmark.ips do |x|
          x.report('Iodine AES-128-GCM encrypt') do
            Iodine::Base::Crypto::AES128GCM.encrypt(plaintext, key: key_16, nonce: nonce_12, ad: ad)
          end

          if defined?(OpenSSL)
            x.report('OpenSSL AES-128-GCM encrypt') do
              cipher = OpenSSL::Cipher.new('aes-128-gcm')
              cipher.encrypt
              cipher.key = key_16
              cipher.iv = nonce_12
              cipher.auth_data = ad
              cipher.update(plaintext)
              cipher.final
              cipher.auth_tag
            end
          end

          x.compare!
        end

        # AES-256-GCM benchmark
        puts "\nAES-256-GCM (32-byte key, 12-byte nonce):"
        ::Benchmark.ips do |x|
          x.report('Iodine AES-256-GCM encrypt') do
            Iodine::Base::Crypto::AES256GCM.encrypt(plaintext, key: key_32, nonce: nonce_12, ad: ad)
          end

          if defined?(OpenSSL)
            x.report('OpenSSL AES-256-GCM encrypt') do
              cipher = OpenSSL::Cipher.new('aes-256-gcm')
              cipher.encrypt
              cipher.key = key_32
              cipher.iv = nonce_12
              cipher.auth_data = ad
              cipher.update(plaintext)
              cipher.final
              cipher.auth_tag
            end
          end

          x.compare!
        end
      end

      nil
    end

    # Benchmarks Iodine's asymmetric cryptography vs OpenSSL/RbNaCl equivalents.
    #
    # Tests Ed25519 signing/verification and X25519 key exchange.
    #
    # Optional dependencies: OpenSSL (>= 1.1.1 for Ed25519/X25519), RbNaCl gem.
    def self.crypto_asymmetric
      require 'benchmark/ips'
      begin
        require 'openssl'
      rescue LoadError
        nil
      end
      begin
        require 'rbnacl'
      rescue LoadError
        nil
      end

      puts '=' * 70
      puts 'Asymmetric Cryptography Benchmarks'
      puts '=' * 70

      # Ed25519 Signing
      puts "\n--- Ed25519 Digital Signatures ---\n\n"

      iodine_sk, iodine_pk = Iodine::Base::Crypto::Ed25519.keypair
      message_short = 'Short message for signing'
      message_long = Iodine::Utils.random(4096)

      # Prepare OpenSSL Ed25519 keys if available
      openssl_ed25519_available = false
      if defined?(OpenSSL)
        begin
          openssl_ed_key = OpenSSL::PKey.generate_key('ED25519')
          openssl_ed25519_available = true
        rescue StandardError => e
          puts "OpenSSL Ed25519 not available: #{e.message}"
        end
      end

      # Prepare RbNaCl keys if available
      rbnacl_available = defined?(RbNaCl)
      if rbnacl_available
        rbnacl_signing_key = RbNaCl::SigningKey.new(iodine_sk)
        rbnacl_verify_key = rbnacl_signing_key.verify_key
      end

      puts 'Ed25519 Key Generation:'
      ::Benchmark.ips do |x|
        x.report('Iodine Ed25519.keypair') do
          Iodine::Base::Crypto::Ed25519.keypair
        end

        if openssl_ed25519_available
          x.report('OpenSSL Ed25519 generate') do
            OpenSSL::PKey.generate_key('ED25519')
          end
        end

        if rbnacl_available
          x.report('RbNaCl SigningKey.generate') do
            RbNaCl::SigningKey.generate
          end
        end

        x.compare!
      end

      puts "\nEd25519 Signing (short message: #{message_short.bytesize} bytes):"
      ::Benchmark.ips do |x|
        x.report('Iodine Ed25519.sign') do
          Iodine::Base::Crypto::Ed25519.sign(message_short, secret_key: iodine_sk, public_key: iodine_pk)
        end

        if openssl_ed25519_available
          x.report('OpenSSL Ed25519 sign') do
            openssl_ed_key.sign(nil, message_short)
          end
        end

        if rbnacl_available
          x.report('RbNaCl sign') do
            rbnacl_signing_key.sign(message_short)
          end
        end

        x.compare!
      end

      puts "\nEd25519 Signing (long message: #{message_long.bytesize} bytes):"
      ::Benchmark.ips do |x|
        x.report('Iodine Ed25519.sign') do
          Iodine::Base::Crypto::Ed25519.sign(message_long, secret_key: iodine_sk, public_key: iodine_pk)
        end

        if openssl_ed25519_available
          x.report('OpenSSL Ed25519 sign') do
            openssl_ed_key.sign(nil, message_long)
          end
        end

        if rbnacl_available
          x.report('RbNaCl sign') do
            rbnacl_signing_key.sign(message_long)
          end
        end

        x.compare!
      end

      # Verification benchmark
      iodine_signature = Iodine::Base::Crypto::Ed25519.sign(message_short, secret_key: iodine_sk, public_key: iodine_pk)

      puts "\nEd25519 Verification (short message):"
      ::Benchmark.ips do |x|
        x.report('Iodine Ed25519.verify') do
          Iodine::Base::Crypto::Ed25519.verify(iodine_signature, message_short, public_key: iodine_pk)
        end

        if openssl_ed25519_available
          openssl_sig = openssl_ed_key.sign(nil, message_short)
          x.report('OpenSSL Ed25519 verify') do
            openssl_ed_key.verify(nil, openssl_sig, message_short)
          end
        end

        if rbnacl_available
          rbnacl_sig = rbnacl_signing_key.sign(message_short)
          x.report('RbNaCl verify') do
            rbnacl_verify_key.verify(rbnacl_sig, message_short)
          end
        end

        x.compare!
      end

      # X25519 Key Exchange
      puts "\n--- X25519 Key Exchange (ECDH) ---\n\n"

      alice_sk, = Iodine::Base::Crypto::X25519.keypair
      bob_sk, bob_pk = Iodine::Base::Crypto::X25519.keypair

      # Prepare OpenSSL X25519 keys if available
      openssl_x25519_available = false
      if defined?(OpenSSL)
        begin
          openssl_alice = OpenSSL::PKey.generate_key('X25519')
          openssl_bob = OpenSSL::PKey.generate_key('X25519')
          openssl_x25519_available = true
        rescue StandardError => e
          puts "OpenSSL X25519 not available: #{e.message}"
        end
      end

      # Prepare RbNaCl keys if available
      if rbnacl_available
        rbnacl_alice = RbNaCl::PrivateKey.new(alice_sk)
        rbnacl_bob = RbNaCl::PrivateKey.new(bob_sk)
      end

      puts 'X25519 Key Generation:'
      ::Benchmark.ips do |x|
        x.report('Iodine X25519.keypair') do
          Iodine::Base::Crypto::X25519.keypair
        end

        if openssl_x25519_available
          x.report('OpenSSL X25519 generate') do
            OpenSSL::PKey.generate_key('X25519')
          end
        end

        if rbnacl_available
          x.report('RbNaCl PrivateKey.generate') do
            RbNaCl::PrivateKey.generate
          end
        end

        x.compare!
      end

      puts "\nX25519 Shared Secret Computation:"
      ::Benchmark.ips do |x|
        x.report('Iodine X25519.shared_secret') do
          Iodine::Base::Crypto::X25519.shared_secret(secret_key: alice_sk, their_public: bob_pk)
        end

        if openssl_x25519_available
          x.report('OpenSSL X25519 derive') do
            openssl_alice.derive(openssl_bob)
          end
        end

        if rbnacl_available
          x.report('RbNaCl GroupElement') do
            RbNaCl::GroupElement.new(rbnacl_bob.public_key.to_bytes).mult(rbnacl_alice.to_bytes)
          end
        end

        x.compare!
      end

      nil
    end

    # Benchmarks Iodine's hashing functions vs OpenSSL/Digest equivalents.
    #
    # Tests SHA-256, SHA-512, SHA3-256, SHA3-512, BLAKE2b, and BLAKE2s.
    #
    # Requires OpenSSL for comparison benchmarks.
    def self.hashing
      require 'benchmark/ips'
      begin
        require 'openssl'
      rescue LoadError
        nil
      end
      begin
        require 'digest'
      rescue LoadError
        nil
      end

      puts '=' * 70
      puts 'Hashing Benchmarks'
      puts '=' * 70

      # Test data sizes
      sizes = {
        '64B' => 64,
        '1KB' => 1024,
        '64KB' => 65_536
      }

      sizes.each do |size_name, size|
        data = Iodine::Utils.random(size)

        puts "\n--- Data Size: #{size_name} ---\n\n"

        # SHA-256
        puts 'SHA-256:'
        ::Benchmark.ips do |x|
          x.report('Iodine sha256') do
            Iodine::Utils.sha256(data)
          end

          if defined?(OpenSSL)
            x.report('OpenSSL SHA256') do
              OpenSSL::Digest::SHA256.digest(data)
            end
          end

          if defined?(Digest)
            x.report('Digest::SHA256') do
              Digest::SHA256.digest(data)
            end
          end

          x.compare!
        end

        # SHA-512
        puts "\nSHA-512:"
        ::Benchmark.ips do |x|
          x.report('Iodine sha512') do
            Iodine::Utils.sha512(data)
          end

          if defined?(OpenSSL)
            x.report('OpenSSL SHA512') do
              OpenSSL::Digest::SHA512.digest(data)
            end
          end

          if defined?(Digest)
            x.report('Digest::SHA512') do
              Digest::SHA512.digest(data)
            end
          end

          x.compare!
        end

        # SHA3-256
        puts "\nSHA3-256:"
        ::Benchmark.ips do |x|
          x.report('Iodine sha3_256') do
            Iodine::Utils.sha3_256(data)
          end

          if defined?(OpenSSL)
            begin
              # OpenSSL 1.1.1+ supports SHA3
              OpenSSL::Digest.new('SHA3-256')
              x.report('OpenSSL SHA3-256') do
                OpenSSL::Digest.new('SHA3-256').digest(data)
              end
            rescue OpenSSL::Digest::DigestError
              # SHA3 not available in this OpenSSL version
            end
          end

          x.compare!
        end

        # SHA3-512
        puts "\nSHA3-512:"
        ::Benchmark.ips do |x|
          x.report('Iodine sha3_512') do
            Iodine::Utils.sha3_512(data)
          end

          if defined?(OpenSSL)
            begin
              OpenSSL::Digest.new('SHA3-512')
              x.report('OpenSSL SHA3-512') do
                OpenSSL::Digest.new('SHA3-512').digest(data)
              end
            rescue OpenSSL::Digest::DigestError
              # SHA3 not available
            end
          end

          x.compare!
        end

        # BLAKE2b
        puts "\nBLAKE2b (64-byte output):"
        ::Benchmark.ips do |x|
          x.report('Iodine blake2b') do
            Iodine::Utils.blake2b(data, len: 64)
          end

          if defined?(OpenSSL)
            begin
              OpenSSL::Digest.new('BLAKE2b512')
              x.report('OpenSSL BLAKE2b512') do
                OpenSSL::Digest.new('BLAKE2b512').digest(data)
              end
            rescue OpenSSL::Digest::DigestError
              # BLAKE2 not available
            end
          end

          x.compare!
        end

        # BLAKE2s
        puts "\nBLAKE2s (32-byte output):"
        ::Benchmark.ips do |x|
          x.report('Iodine blake2s') do
            Iodine::Utils.blake2s(data, len: 32)
          end

          if defined?(OpenSSL)
            begin
              OpenSSL::Digest.new('BLAKE2s256')
              x.report('OpenSSL BLAKE2s256') do
                OpenSSL::Digest.new('BLAKE2s256').digest(data)
              end
            rescue OpenSSL::Digest::DigestError
              # BLAKE2s not available
            end
          end

          x.compare!
        end

        # Keyed BLAKE2b (MAC mode)
        next unless size <= 1024 # Only for smaller sizes to keep benchmark reasonable

        key = Iodine::Utils.random(32)
        puts "\nBLAKE2b Keyed (MAC mode, 32-byte key):"
        ::Benchmark.ips do |x|
          x.report('Iodine blake2b keyed') do
            Iodine::Utils.blake2b(data, key: key, len: 32)
          end

          # Compare with HMAC-SHA256 as alternative MAC
          if defined?(OpenSSL)
            x.report('OpenSSL HMAC-SHA256') do
              OpenSSL::HMAC.digest('SHA256', key, data)
            end
          end

          x.compare!
        end
      end

      nil
    end

    # Benchmarks Iodine's X25519 ECIES public-key encryption vs RbNaCl sealed boxes.
    #
    # Tests encryption/decryption with various payload sizes.
    #
    # Optional dependencies: RbNaCl gem for comparison.
    def self.crypto_ecies
      require 'benchmark/ips'
      begin
        require 'rbnacl'
      rescue LoadError
        nil
      end

      puts '=' * 70
      puts 'X25519 ECIES (Public-Key Encryption) Benchmarks'
      puts '=' * 70

      # Generate recipient keypair
      recipient_sk, recipient_pk = Iodine::Base::Crypto::X25519.keypair

      # Test data sizes
      sizes = {
        '64B' => 64,
        '1KB' => 1024,
        '16KB' => 16_384
      }

      rbnacl_available = defined?(RbNaCl)
      if rbnacl_available
        rbnacl_pk = RbNaCl::PrivateKey.new(recipient_sk).public_key
        rbnacl_sk = RbNaCl::PrivateKey.new(recipient_sk)
      end

      sizes.each do |size_name, size|
        plaintext = Iodine::Utils.random(size)

        puts "\n--- Payload Size: #{size_name} ---\n\n"

        # Encryption benchmarks
        puts 'X25519 ECIES Encryption (ChaCha20-Poly1305):'
        ::Benchmark.ips do |x|
          x.report('Iodine X25519.encrypt') do
            Iodine::Base::Crypto::X25519.encrypt(plaintext, recipient_pk: recipient_pk)
          end

          if rbnacl_available
            x.report('RbNaCl SimpleBox seal') do
              RbNaCl::SimpleBox.from_public_key(rbnacl_pk).encrypt(plaintext)
            end
          end

          x.compare!
        end

        # Decryption benchmarks
        iodine_ciphertext = Iodine::Base::Crypto::X25519.encrypt(plaintext, recipient_pk: recipient_pk)

        puts "\nX25519 ECIES Decryption (ChaCha20-Poly1305):"
        ::Benchmark.ips do |x|
          x.report('Iodine X25519.decrypt') do
            Iodine::Base::Crypto::X25519.decrypt(iodine_ciphertext, secret_key: recipient_sk)
          end

          if rbnacl_available
            rbnacl_ciphertext = RbNaCl::SimpleBox.from_public_key(rbnacl_pk).encrypt(plaintext)
            x.report('RbNaCl SimpleBox open') do
              RbNaCl::SimpleBox.from_keypair(rbnacl_pk, rbnacl_sk).decrypt(rbnacl_ciphertext)
            end
          end

          x.compare!
        end

        # Compare different ECIES cipher variants
        puts "\nIodine X25519 ECIES Cipher Variants (encryption):"
        ::Benchmark.ips do |x|
          x.report('X25519.encrypt (ChaCha20)') do
            Iodine::Base::Crypto::X25519.encrypt(plaintext, recipient_pk: recipient_pk)
          end

          x.report('X25519.encrypt_aes128') do
            Iodine::Base::Crypto::X25519.encrypt_aes128(plaintext, recipient_pk: recipient_pk)
          end

          x.report('X25519.encrypt_aes256') do
            Iodine::Base::Crypto::X25519.encrypt_aes256(plaintext, recipient_pk: recipient_pk)
          end

          x.compare!
        end
      end

      nil
    end

    # Benchmarks Iodine's HKDF key derivation vs OpenSSL HKDF.
    #
    # Tests key derivation with various output lengths.
    #
    # Requires OpenSSL >= 1.1.0 for comparison benchmarks.
    def self.crypto_kdf
      require 'benchmark/ips'
      begin
        require 'openssl'
      rescue LoadError
        nil
      end

      puts '=' * 70
      puts 'HKDF Key Derivation Benchmarks'
      puts '=' * 70

      # Input keying material (e.g., from X25519 shared secret)
      ikm = Iodine::Utils.random(32)
      salt = Iodine::Utils.random(32)
      info = 'application specific context'

      # Output lengths to test
      lengths = [16, 32, 64, 128]

      # Check if OpenSSL HKDF is available
      openssl_hkdf_available = false
      if defined?(OpenSSL) && defined?(OpenSSL::KDF)
        begin
          OpenSSL::KDF.hkdf(ikm, salt: salt, info: info, length: 32, hash: 'SHA256')
          openssl_hkdf_available = true
        rescue StandardError => e
          puts "OpenSSL HKDF not available: #{e.message}"
        end
      end

      lengths.each do |length|
        puts "\n--- Output Length: #{length} bytes ---\n\n"

        ::Benchmark.ips do |x|
          x.report('Iodine HKDF.derive (SHA-256)') do
            Iodine::Base::Crypto::HKDF.derive(ikm: ikm, salt: salt, info: info, length: length, sha384: false)
          end

          x.report('Iodine HKDF.derive (SHA-384)') do
            Iodine::Base::Crypto::HKDF.derive(ikm: ikm, salt: salt, info: info, length: length, sha384: true)
          end

          if openssl_hkdf_available
            x.report('OpenSSL HKDF (SHA-256)') do
              OpenSSL::KDF.hkdf(ikm, salt: salt, info: info, length: length, hash: 'SHA256')
            end

            x.report('OpenSSL HKDF (SHA-384)') do
              OpenSSL::KDF.hkdf(ikm, salt: salt, info: info, length: length, hash: 'SHA384')
            end
          end

          x.compare!
        end
      end

      nil
    end

    # Benchmarks Iodine's TOTP implementation vs ROTP gem.
    #
    # Tests TOTP generation and verification.
    #
    # Optional dependencies: rotp gem for comparison.
    def self.crypto_totp
      require 'benchmark/ips'
      begin
        require 'rotp'
      rescue LoadError
        nil
      end

      puts '=' * 70
      puts 'TOTP (Time-based One-Time Password) Benchmarks'
      puts '=' * 70

      # Generate a TOTP secret
      secret = Iodine::Utils.totp_secret(len: 20)
      # Decode for Iodine (it expects raw bytes)
      # Note: Iodine's totp expects the raw secret, not base32 encoded
      raw_secret = Iodine::Utils.random(20)

      rotp_available = defined?(ROTP)
      rotp_totp = ROTP::TOTP.new(secret) if rotp_available

      puts "\nTOTP Secret Generation:"
      ::Benchmark.ips do |x|
        x.report('Iodine totp_secret') do
          Iodine::Utils.totp_secret(len: 20)
        end

        if rotp_available
          x.report('ROTP random_base32') do
            ROTP::Base32.random_base32(32)
          end
        end

        x.compare!
      end

      puts "\nTOTP Code Generation:"
      ::Benchmark.ips do |x|
        x.report('Iodine totp') do
          Iodine::Utils.totp(secret: raw_secret)
        end

        if rotp_available
          x.report('ROTP now') do
            rotp_totp.now
          end
        end

        x.compare!
      end

      puts "\nTOTP Verification (window: 1):"
      current_code = Iodine::Utils.totp(secret: raw_secret)

      ::Benchmark.ips do |x|
        x.report('Iodine totp_verify') do
          Iodine::Utils.totp_verify(secret: raw_secret, code: current_code, window: 1)
        end

        if rotp_available
          rotp_code = rotp_totp.now.to_s
          x.report('ROTP verify') do
            rotp_totp.verify(rotp_code, drift_behind: 30, drift_ahead: 30)
          end
        end

        x.compare!
      end

      nil
    end

    # Runs all crypto benchmarks.
    #
    # This is a convenience method that runs all crypto-related benchmarks:
    # - crypto_symmetric (AEAD ciphers)
    # - crypto_asymmetric (Ed25519, X25519)
    # - hashing (SHA-2, SHA-3, BLAKE2)
    # - crypto_ecies (public-key encryption)
    # - crypto_kdf (HKDF key derivation)
    # - crypto_totp (TOTP generation/verification)
    def self.crypto_all
      puts "\n" + '=' * 70
      puts 'Running All Crypto Benchmarks'
      puts '=' * 70 + "\n"

      crypto_symmetric
      puts "\n"
      crypto_asymmetric
      puts "\n"
      hashing
      puts "\n"
      crypto_ecies
      puts "\n"
      crypto_kdf
      puts "\n"
      crypto_totp

      puts "\n" + '=' * 70
      puts 'All Crypto Benchmarks Complete'
      puts '=' * 70

      nil
    end
  end
end
