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

      # -----------------------------------------------------------------------
      # HMAC benchmarks — all MACs (cryptographic and non-cryptographic) ranked
      #
      # NOTE on algorithm mapping:
      #   hmac128       => Poly1305 MAC (not HMAC-SHA1/SHA256; no standard OpenSSL equivalent)
      #   hmac160       => HMAC-SHA1  (compared against OpenSSL HMAC-SHA1)
      #   hmac256       => HMAC-SHA256 (compared against OpenSSL HMAC-SHA256)
      #   hmac512       => HMAC-SHA512 (compared against OpenSSL HMAC-SHA512)
      #   risky256_hmac => keyed MAC on non-crypto Risky Hash — NOT for security-critical use
      #   risky512_hmac => keyed MAC on non-crypto Risky Hash — NOT for security-critical use
      #
      # Payload: 512 bytes of the benchmark file itself (representative of a
      # short-to-medium web token or session payload).
      # -----------------------------------------------------------------------
      benchmark_secret = Iodine.secret
      benchmark_payload = (File.exist?(__FILE__) ? IO.binread(__FILE__)[0...512] : 'my message payload')

      puts "HMAC benchmarks — all MACs ranked — #{benchmark_payload.bytesize}-byte message, #{benchmark_secret.bytesize}-byte secret"
      puts "  [WARNING: risky256_hmac / risky512_hmac are NOT suitable for security-critical authentication]"

      # Pre-create OpenSSL digest objects outside the hot loop so we measure
      # only the HMAC computation, not object allocation.
      if defined?(OpenSSL)
        digest_sha1   = OpenSSL::Digest.new('SHA1')
        digest_sha256 = OpenSSL::Digest.new('SHA256')
        digest_sha512 = OpenSSL::Digest.new('SHA512')
      end

      ::Benchmark.ips do |bm|
        # Non-cryptographic MACs — no standard equivalent; included for throughput ranking
        bm.report('Iodine::Utils.risky256_hmac (non-crypto, 32B)') do
          Iodine::Utils.risky256_hmac(benchmark_secret, benchmark_payload)
        end
        bm.report('Iodine::Utils.risky512_hmac (non-crypto, 64B)') do
          Iodine::Utils.risky512_hmac(benchmark_secret, benchmark_payload)
        end
        # Poly1305 MAC — no direct OpenSSL equivalent; shown for reference only
        bm.report('Iodine::Utils.hmac128 (Poly1305)         ') do
          Iodine::Utils.hmac128(benchmark_secret, benchmark_payload)
        end
        # HMAC-SHA1
        bm.report('Iodine::Utils.hmac160 (HMAC-SHA1)        ') do
          Iodine::Utils.hmac160(benchmark_secret, benchmark_payload)
        end
        # HMAC-SHA256
        bm.report('Iodine::Utils.hmac256 (HMAC-SHA256)      ') do
          Iodine::Utils.hmac256(benchmark_secret, benchmark_payload)
        end
        # HMAC-SHA512
        bm.report('Iodine::Utils.hmac512 (HMAC-SHA512)      ') do
          Iodine::Utils.hmac512(benchmark_secret, benchmark_payload)
        end
        if defined?(OpenSSL)
          bm.report('OpenSSL::HMAC.digest SHA1                ') do
            OpenSSL::HMAC.digest(digest_sha1, benchmark_secret, benchmark_payload)
          end
          bm.report('OpenSSL::HMAC.digest SHA256              ') do
            OpenSSL::HMAC.digest(digest_sha256, benchmark_secret, benchmark_payload)
          end
          bm.report('OpenSSL::HMAC.digest SHA512              ') do
            OpenSSL::HMAC.digest(digest_sha512, benchmark_secret, benchmark_payload)
          end
        end
        bm.compare!
      end

      # -----------------------------------------------------------------------
      # UUID generation
      # -----------------------------------------------------------------------
      puts "\nUUID generation (random v4)"
      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.uuid') { Iodine::Utils.uuid }
        bm.report(' SecureRandom.uuid') { SecureRandom.uuid }
        bm.report('    Random.uuid_v4') { Random.uuid_v4 }
        bm.compare!
      end

      # -----------------------------------------------------------------------
      # Random bytes generation — all generators ranked per size
      #
      # Iodine::Utils.random uses a fast PRNG (not cryptographically secure).
      # Iodine::Utils.secure_random uses arc4random_buf (BSD/macOS) or
      # /dev/urandom (Linux) — suitable for key generation and nonces.
      # Compared against SecureRandom.random_bytes and OpenSSL::Random.random_bytes.
      # -----------------------------------------------------------------------
      puts "\nRandom bytes — all generators ranked — 16 bytes"
      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.random(16)              ') { Iodine::Utils.random(16) }
        bm.report('Random.bytes(16)                      ') { ::Random.bytes(16) }
        bm.report('Iodine::Utils.secure_random(bytes: 16)') { Iodine::Utils.secure_random(bytes: 16) }
        bm.report('SecureRandom.random_bytes(16)         ') { SecureRandom.random_bytes(16) }
        if defined?(OpenSSL)
          bm.report('OpenSSL::Random.random_bytes(16)      ') { OpenSSL::Random.random_bytes(16) }
        end
        bm.compare!
      end

      puts "\nRandom bytes — all generators ranked — 4096 bytes"
      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.random(4096)              ') { Iodine::Utils.random(4096) }
        bm.report('Random.bytes(4096)                      ') { ::Random.bytes(4096) }
        bm.report('Iodine::Utils.secure_random(bytes: 4096)') { Iodine::Utils.secure_random(bytes: 4096) }
        bm.report('SecureRandom.random_bytes(4096)         ') { SecureRandom.random_bytes(4096) }
        if defined?(OpenSSL)
          bm.report('OpenSSL::Random.random_bytes(4096)      ') { OpenSSL::Random.random_bytes(4096) }
        end
        bm.compare!
      end

      puts "\nRandom float (0.0..1.0)"
      ::Benchmark.ips do |bm|
        bm.report('Iodine::Utils.random') { Iodine::Utils.random }
        bm.report('   SecureRandom.rand') { SecureRandom.rand }
        bm.report('         Random.rand') { ::Random.rand }
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
    #
    # FAIRNESS NOTES:
    # - All ciphertexts for decryption are pre-computed OUTSIDE the hot loop so
    #   that only the actual encrypt/decrypt operation is timed.
    # - OpenSSL cipher setup (new/encrypt/key/iv/auth_data) is inside the hot
    #   loop because Iodine also re-initialises state per call — both sides pay
    #   the same per-call setup cost.
    # - XChaCha20-Poly1305 has no OpenSSL equivalent; it is included in the
    #   unified block with a label suffix so its throughput is visible in context.
    # - All implementations use the same key, nonce, and additional data.
    def self.crypto_symmetric
      require 'benchmark/ips'
      begin
        require 'openssl'
      rescue LoadError
        nil
      end

      # Test data sizes
      sizes = {
        '64B'  => 64,
        '1KB'  => 1024,
        '64KB' => 65_536
      }

      # Keys and nonces — generated once, reused across all benchmarks
      key_32    = Iodine::Utils.random(32)
      key_16    = Iodine::Utils.random(16)
      nonce_12  = Iodine::Utils.random(12)
      nonce_24  = Iodine::Utils.random(24)
      ad        = 'additional authenticated data'

      # Check which OpenSSL ciphers are available
      openssl_chacha_available = defined?(OpenSSL) &&
                                 OpenSSL::Cipher.ciphers.include?('chacha20-poly1305')
      openssl_aes_available    = defined?(OpenSSL)

      # Label width — wide enough for 'Iodine XChaCha20-Poly1305 encrypt (24B nonce)'
      lw = 36

      puts '=' * 70
      puts 'AEAD Symmetric Encryption Benchmarks'
      puts '=' * 70
      puts
      puts 'NOTE: All ciphertexts are pre-computed outside the hot loop.'
      puts 'OpenSSL cipher setup is inside the loop (same cost as Iodine).'
      puts 'XChaCha20-Poly1305 has no OpenSSL equivalent — included in the'
      puts 'unified block with a label note so throughput is visible in context.'
      puts

      sizes.each do |size_name, size|
        plaintext = Iodine::Utils.random(size)

        puts "\n--- Payload Size: #{size_name} — all ciphers ranked together ---\n\n"

        # ------------------------------------------------------------------
        # Pre-compute all ciphertexts outside the hot loop so that only the
        # decrypt operation itself is timed in the decrypt block below.
        # ------------------------------------------------------------------
        iodine_ct_chacha, iodine_mac_chacha =
          Iodine::Base::Crypto::ChaCha20Poly1305.encrypt(plaintext, key: key_32, nonce: nonce_12, ad: ad)

        iodine_ct_xchacha, iodine_mac_xchacha =
          Iodine::Base::Crypto::XChaCha20Poly1305.encrypt(plaintext, key: key_32, nonce: nonce_24, ad: ad)

        iodine_ct_aes128, iodine_mac_aes128 =
          Iodine::Base::Crypto::AES128GCM.encrypt(plaintext, key: key_16, nonce: nonce_12, ad: ad)

        iodine_ct_aes256, iodine_mac_aes256 =
          Iodine::Base::Crypto::AES256GCM.encrypt(plaintext, key: key_32, nonce: nonce_12, ad: ad)

        openssl_ct_chacha = nil
        openssl_tag_chacha = nil
        if openssl_chacha_available
          c = OpenSSL::Cipher.new('chacha20-poly1305')
          c.encrypt
          c.key       = key_32
          c.iv        = nonce_12
          c.auth_data = ad
          openssl_ct_chacha  = c.update(plaintext) + c.final
          openssl_tag_chacha = c.auth_tag
        end

        openssl_ct_aes128 = nil
        openssl_tag_aes128 = nil
        openssl_ct_aes256 = nil
        openssl_tag_aes256 = nil
        if openssl_aes_available
          c = OpenSSL::Cipher.new('aes-128-gcm')
          c.encrypt
          c.key       = key_16
          c.iv        = nonce_12
          c.auth_data = ad
          openssl_ct_aes128  = c.update(plaintext) + c.final
          openssl_tag_aes128 = c.auth_tag

          c = OpenSSL::Cipher.new('aes-256-gcm')
          c.encrypt
          c.key       = key_32
          c.iv        = nonce_12
          c.auth_data = ad
          openssl_ct_aes256  = c.update(plaintext) + c.final
          openssl_tag_aes256 = c.auth_tag
        end

        # ------------------------------------------------------------------
        # Unified encrypt block — all ciphers compete side-by-side.
        # XChaCha20 has no OpenSSL equivalent — labelled with (no OSSl equiv).
        # x.compare! prints a ranked table so the fastest cipher is clear.
        # ------------------------------------------------------------------
        puts "Encrypt — all ciphers ranked:"
        puts "  (XChaCha20-Poly1305 has no OpenSSL equivalent — included for ranking context)"
        ::Benchmark.ips do |x|
          x.report('Iodine ChaCha20-Poly1305 encrypt'.ljust(lw)) do
            Iodine::Base::Crypto::ChaCha20Poly1305.encrypt(plaintext, key: key_32, nonce: nonce_12, ad: ad)
          end
          if openssl_chacha_available
            # OpenSSL cipher setup is inside the loop — same per-call cost as Iodine
            x.report('OpenSSL ChaCha20-Poly1305 encrypt'.ljust(lw)) do
              c = OpenSSL::Cipher.new('chacha20-poly1305')
              c.encrypt
              c.key       = key_32
              c.iv        = nonce_12
              c.auth_data = ad
              _ = c.update(plaintext) + c.final
              c.auth_tag
            end
          end
          # XChaCha20 has no OpenSSL equivalent — included for ranking context
          x.report('Iodine XChaCha20-Poly1305 encrypt (24B nonce)'.ljust(lw)) do
            Iodine::Base::Crypto::XChaCha20Poly1305.encrypt(plaintext, key: key_32, nonce: nonce_24, ad: ad)
          end
          x.report('Iodine AES-128-GCM encrypt'.ljust(lw)) do
            Iodine::Base::Crypto::AES128GCM.encrypt(plaintext, key: key_16, nonce: nonce_12, ad: ad)
          end
          if openssl_aes_available
            x.report('OpenSSL AES-128-GCM encrypt'.ljust(lw)) do
              c = OpenSSL::Cipher.new('aes-128-gcm')
              c.encrypt
              c.key       = key_16
              c.iv        = nonce_12
              c.auth_data = ad
              _ = c.update(plaintext) + c.final
              c.auth_tag
            end
          end
          x.report('Iodine AES-256-GCM encrypt'.ljust(lw)) do
            Iodine::Base::Crypto::AES256GCM.encrypt(plaintext, key: key_32, nonce: nonce_12, ad: ad)
          end
          if openssl_aes_available
            x.report('OpenSSL AES-256-GCM encrypt'.ljust(lw)) do
              c = OpenSSL::Cipher.new('aes-256-gcm')
              c.encrypt
              c.key       = key_32
              c.iv        = nonce_12
              c.auth_data = ad
              _ = c.update(plaintext) + c.final
              c.auth_tag
            end
          end
          x.compare!
        end

        # ------------------------------------------------------------------
        # Unified decrypt block — all ciphers compete side-by-side.
        # Ciphertexts were pre-computed above (outside the hot loop).
        # x.compare! prints a ranked table so the fastest cipher is clear.
        # ------------------------------------------------------------------
        puts "\nDecrypt — all ciphers ranked:"
        puts "  (XChaCha20-Poly1305 has no OpenSSL equivalent — included for ranking context)"
        ::Benchmark.ips do |x|
          x.report('Iodine ChaCha20-Poly1305 decrypt'.ljust(lw)) do
            Iodine::Base::Crypto::ChaCha20Poly1305.decrypt(
              iodine_ct_chacha, mac: iodine_mac_chacha, key: key_32, nonce: nonce_12, ad: ad
            )
          end
          if openssl_chacha_available
            x.report('OpenSSL ChaCha20-Poly1305 decrypt'.ljust(lw)) do
              d = OpenSSL::Cipher.new('chacha20-poly1305')
              d.decrypt
              d.key       = key_32
              d.iv        = nonce_12
              d.auth_data = ad
              d.auth_tag  = openssl_tag_chacha
              d.update(openssl_ct_chacha) + d.final
            end
          end
          # XChaCha20 has no OpenSSL equivalent — included for ranking context
          x.report('Iodine XChaCha20-Poly1305 decrypt (24B nonce)'.ljust(lw)) do
            Iodine::Base::Crypto::XChaCha20Poly1305.decrypt(
              iodine_ct_xchacha, mac: iodine_mac_xchacha, key: key_32, nonce: nonce_24, ad: ad
            )
          end
          x.report('Iodine AES-128-GCM decrypt'.ljust(lw)) do
            Iodine::Base::Crypto::AES128GCM.decrypt(
              iodine_ct_aes128, mac: iodine_mac_aes128, key: key_16, nonce: nonce_12, ad: ad
            )
          end
          if openssl_aes_available
            x.report('OpenSSL AES-128-GCM decrypt'.ljust(lw)) do
              d = OpenSSL::Cipher.new('aes-128-gcm')
              d.decrypt
              d.key       = key_16
              d.iv        = nonce_12
              d.auth_data = ad
              d.auth_tag  = openssl_tag_aes128
              d.update(openssl_ct_aes128) + d.final
            end
          end
          x.report('Iodine AES-256-GCM decrypt'.ljust(lw)) do
            Iodine::Base::Crypto::AES256GCM.decrypt(
              iodine_ct_aes256, mac: iodine_mac_aes256, key: key_32, nonce: nonce_12, ad: ad
            )
          end
          if openssl_aes_available
            x.report('OpenSSL AES-256-GCM decrypt'.ljust(lw)) do
              d = OpenSSL::Cipher.new('aes-256-gcm')
              d.decrypt
              d.key       = key_32
              d.iv        = nonce_12
              d.auth_data = ad
              d.auth_tag  = openssl_tag_aes256
              d.update(openssl_ct_aes256) + d.final
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

      # Prepare RbNaCl keys if available — probe via actual instantiation so
      # that a partially-loaded gem (defined?(RbNaCl) true but sub-constants
      # missing) is caught cleanly rather than raising NameError later.
      rbnacl_available = false
      rbnacl_signing_key = nil
      rbnacl_verify_key  = nil
      begin
        rbnacl_signing_key = RbNaCl::SigningKey.new(iodine_sk)
        rbnacl_verify_key  = rbnacl_signing_key.verify_key
        rbnacl_available   = true
      rescue NameError, LoadError => e
        puts "RbNaCl not available (#{e.message}); skipping RbNaCl comparisons."
      end

      # Label width — wide enough for 'Iodine Ed25519 sign (4KB)          '
      lw = 34

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

      # ------------------------------------------------------------------
      # Unified signing block — both message sizes compete side-by-side.
      # This lets readers see how message length affects signing throughput.
      # x.compare! prints a ranked table across all entries.
      # ------------------------------------------------------------------
      puts "\nEd25519 Signing — all implementations and message sizes ranked:"
      ::Benchmark.ips do |x|
        x.report("Iodine Ed25519 sign (#{message_short.bytesize}B)".ljust(lw)) do
          Iodine::Base::Crypto::Ed25519.sign(message_short, secret_key: iodine_sk, public_key: iodine_pk)
        end
        x.report("Iodine Ed25519 sign (#{message_long.bytesize / 1024}KB)".ljust(lw)) do
          Iodine::Base::Crypto::Ed25519.sign(message_long, secret_key: iodine_sk, public_key: iodine_pk)
        end
        if openssl_ed25519_available
          x.report("OpenSSL Ed25519 sign (#{message_short.bytesize}B)".ljust(lw)) do
            openssl_ed_key.sign(nil, message_short)
          end
          x.report("OpenSSL Ed25519 sign (#{message_long.bytesize / 1024}KB)".ljust(lw)) do
            openssl_ed_key.sign(nil, message_long)
          end
        end
        if rbnacl_available
          x.report("RbNaCl sign (#{message_short.bytesize}B)".ljust(lw)) do
            rbnacl_signing_key.sign(message_short)
          end
          x.report("RbNaCl sign (#{message_long.bytesize / 1024}KB)".ljust(lw)) do
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
    # Tests SHA-256, SHA-512, SHA3-224, SHA3-256, SHA3-512, BLAKE2b, BLAKE2s,
    # SHAKE128, SHAKE256, risky256, and risky512 across three payload sizes
    # (64B, 1KB, 64KB). risky256/risky512 are non-cryptographic but are
    # included as faster alternatives suitable for message authentication where
    # cryptographic strength is not required.
    #
    # For each payload size, ALL algorithms compete in a single Benchmark.ips
    # block so results can be used directly for algorithm selection — you can
    # immediately see "at 1KB, which hash is fastest?"
    #
    # Requires OpenSSL for comparison benchmarks.
    #
    # FAIRNESS NOTES:
    # - OpenSSL::Digest objects are pre-instantiated ONCE before the sizes loop
    #   and reused across all iterations. Calling instance.digest(data) resets
    #   the existing EVP_MD_CTX state (via EVP_DigestInit_ex) without allocating
    #   a new context — only the hash computation itself is timed.
    # - By contrast, the class methods OpenSSL::Digest.digest('SHA3-256', data)
    #   and OpenSSL::Digest::SHA256.digest(data) allocate a new EVP_MD_CTX AND
    #   run EVP_get_digestbyname() on every call — unfairly penalising OpenSSL.
    # - Digest::SHA256/SHA512 (Ruby stdlib) delegates to OpenSSL internally.
    #   The same pre-instantiation fix is applied: Digest::SHA256.new is created
    #   once and instance.digest(data) is called in the hot loop.
    # - SHA3 and BLAKE2 availability depends on the OpenSSL build. If not
    #   available, only Iodine results are shown for those algorithms.
    # - SHAKE128/SHAKE256 have no OpenSSL Ruby binding; they are included in
    #   the unified block so their throughput is visible in the ranking.
    #
    # NOTE on digest == hash: Iodine's sha256/sha3_256/blake2b etc. and
    # OpenSSL::Digest both compute cryptographic message digests — they are the
    # same operation (deterministic, fixed-output-length, one-way hash functions
    # over arbitrary input). The comparison is apples-to-apples.
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
      puts
      puts 'NOTE: All algorithms are benchmarked together per payload size so'
      puts 'results can be used for algorithm selection. OpenSSL::Digest and'
      puts 'Digest:: objects are pre-instantiated once before the sizes loop;'
      puts 'instance.digest(data) reuses the EVP_MD_CTX (only resets state) —'
      puts 'no per-call allocation or string lookup overhead.'
      puts

      # Test data sizes
      sizes = {
        '64B'  => 64,
        '1KB'  => 1024,
        '64KB' => 65_536
      }

      # ------------------------------------------------------------------
      # Pre-check which OpenSSL algorithms are available and pre-instantiate
      # digest objects ONCE here — before the sizes loop — so they are
      # allocated exactly once regardless of how many size iterations run.
      # ------------------------------------------------------------------
      openssl_sha3_256_available  = false
      openssl_sha3_512_available  = false
      openssl_blake2b_available   = false
      openssl_blake2s_available   = false
      openssl_sha3_224_available  = false

      # Pre-instantiated digest objects (nil if algorithm unavailable)
      ossl_sha256    = nil
      ossl_sha512    = nil
      ossl_sha3_256  = nil
      ossl_sha3_512  = nil
      ossl_blake2b   = nil
      ossl_blake2s   = nil
      ossl_sha3_224  = nil
      digest_sha256  = nil
      digest_sha512  = nil

      if defined?(OpenSSL)
        # SHA-256 and SHA-512 are always available in any OpenSSL build
        ossl_sha256 = OpenSSL::Digest.new('SHA256')
        ossl_sha512 = OpenSSL::Digest.new('SHA512')

        begin
          ossl_sha3_256 = OpenSSL::Digest.new('SHA3-256')
          ossl_sha3_256.digest('') # probe — raises if unavailable
          openssl_sha3_256_available = true
        rescue OpenSSL::Digest::DigestError, RuntimeError
          ossl_sha3_256 = nil
          # SHA3-256 not available in this OpenSSL build
        end
        begin
          ossl_sha3_512 = OpenSSL::Digest.new('SHA3-512')
          ossl_sha3_512.digest('')
          openssl_sha3_512_available = true
        rescue OpenSSL::Digest::DigestError, RuntimeError
          ossl_sha3_512 = nil
          # SHA3-512 not available
        end
        begin
          ossl_blake2b = OpenSSL::Digest.new('BLAKE2b512')
          ossl_blake2b.digest('')
          openssl_blake2b_available = true
        rescue OpenSSL::Digest::DigestError, RuntimeError
          ossl_blake2b = nil
          # BLAKE2b not available
        end
        begin
          ossl_blake2s = OpenSSL::Digest.new('BLAKE2s256')
          ossl_blake2s.digest('')
          openssl_blake2s_available = true
        rescue OpenSSL::Digest::DigestError, RuntimeError
          ossl_blake2s = nil
          # BLAKE2s not available
        end
        begin
          ossl_sha3_224 = OpenSSL::Digest.new('SHA3-224')
          ossl_sha3_224.digest('')
          openssl_sha3_224_available = true
        rescue OpenSSL::Digest::DigestError, RuntimeError
          ossl_sha3_224 = nil
          # SHA3-224 not available in this OpenSSL build
        end
      end

      if defined?(Digest)
        # Ruby stdlib Digest — pre-instantiate once; instance.digest(data)
        # calls reset+update+finish on the existing object (no allocation).
        digest_sha256 = Digest::SHA256.new
        digest_sha512 = Digest::SHA512.new
      end

      # Label width — wide enough for the longest entry ("Iodine risky512 (non-crypto, 64B)")
      lw = 34

      sizes.each do |size_name, size|
        data = Iodine::Utils.random(size)

        puts "\n--- Data Size: #{size_name} — all algorithms ranked together ---\n\n"
        puts "  (SHAKE128/SHAKE256 have no OpenSSL Ruby binding; risky256/risky512 are non-crypto"
        puts "   alternatives suitable for fast message authentication; all included for ranking context)"
        puts

        # ------------------------------------------------------------------
        # Unified block: all algorithms compete side-by-side for this size.
        # x.compare! prints a ranked table so the fastest algorithm is clear.
        # ------------------------------------------------------------------
        ::Benchmark.ips do |x|
          # ---- Iodine entries (always present) ----
          x.report('Iodine sha256'.ljust(lw))                    { Iodine::Utils.sha256(data) }
          x.report('Iodine sha512'.ljust(lw))                    { Iodine::Utils.sha512(data) }
          x.report('Iodine sha3_224'.ljust(lw))                  { Iodine::Utils.sha3_224(data) }
          x.report('Iodine sha3_256'.ljust(lw))                  { Iodine::Utils.sha3_256(data) }
          x.report('Iodine sha3_512'.ljust(lw))                  { Iodine::Utils.sha3_512(data) }
          x.report('Iodine blake2b (64B)'.ljust(lw))             { Iodine::Utils.blake2b(data, len: 64) }
          x.report('Iodine blake2s (32B)'.ljust(lw))             { Iodine::Utils.blake2s(data, len: 32) }
          x.report('Iodine shake128 (XOF, 32B)'.ljust(lw))       { Iodine::Utils.shake128(data, length: 32) }
          x.report('Iodine shake256 (XOF, 64B)'.ljust(lw))       { Iodine::Utils.shake256(data, length: 64) }
          ## risky256/risky512: non-crypto alternatives for fast message authentication
          # x.report('Iodine risky256 (non-crypto, 32B)'.ljust(lw)) { Iodine::Utils.risky256(data) }
          # x.report('Iodine risky512 (non-crypto, 64B)'.ljust(lw)) { Iodine::Utils.risky512(data) }

          # ---- OpenSSL entries (conditionally present) ----
          if ossl_sha256
            x.report('OpenSSL SHA256'.ljust(lw))      { ossl_sha256.digest(data) }
          end
          if ossl_sha512
            x.report('OpenSSL SHA512'.ljust(lw))      { ossl_sha512.digest(data) }
          end
          if openssl_sha3_224_available
            x.report('OpenSSL SHA3-224'.ljust(lw))    { ossl_sha3_224.digest(data) }
          end
          if openssl_sha3_256_available
            x.report('OpenSSL SHA3-256'.ljust(lw))    { ossl_sha3_256.digest(data) }
          end
          if openssl_sha3_512_available
            x.report('OpenSSL SHA3-512'.ljust(lw))    { ossl_sha3_512.digest(data) }
          end
          if openssl_blake2b_available
            x.report('OpenSSL BLAKE2b512'.ljust(lw))  { ossl_blake2b.digest(data) }
          end
          if openssl_blake2s_available
            x.report('OpenSSL BLAKE2s256'.ljust(lw))  { ossl_blake2s.digest(data) }
          end

          # ---- Ruby stdlib Digest entries (conditionally present) ----
          if digest_sha256
            x.report('Digest::SHA256'.ljust(lw))      { digest_sha256.digest(data) }
          end
          if digest_sha512
            x.report('Digest::SHA512'.ljust(lw))      { digest_sha512.digest(data) }
          end

          puts "\n--- Data Size: #{size_name} ---\n\n"
          x.compare!
        end
      end

      nil
    end

    # Benchmarks Iodine's X25519 ECIES public-key encryption vs RbNaCl sealed boxes.
    #
    # Tests encryption/decryption with various payload sizes and all cipher variants.
    #
    # Optional dependencies: RbNaCl gem for comparison.
    #
    # FAIRNESS NOTES:
    # - All ciphertexts for decryption are pre-computed OUTSIDE the hot loop so
    #   that only the actual decrypt operation is timed.
    # - All three Iodine cipher variants (ChaCha20, AES-128, AES-256) compete in
    #   the unified encrypt block so readers can choose the right cipher.
    # - RbNaCl SimpleBox uses XSalsa20-Poly1305 (different cipher) — included for
    #   throughput reference; label makes the difference clear.
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
        '64B'  => 64,
        '1KB'  => 1024,
        '16KB' => 16_384
      }

      # Label width — wide enough for 'Iodine X25519.decrypt_aes256 (AES-256)'
      lw = 32

      rbnacl_available = false
      rbnacl_pk = nil
      rbnacl_sk = nil
      begin
        rbnacl_sk = RbNaCl::PrivateKey.new(recipient_sk)
        rbnacl_pk = rbnacl_sk.public_key
        rbnacl_available = true
      rescue NameError, LoadError => e
        puts "RbNaCl not available (#{e.message}); skipping RbNaCl comparisons."
      end

      sizes.each do |size_name, size|
        plaintext = Iodine::Utils.random(size)

        puts "\n--- Payload Size: #{size_name} — all cipher variants ranked ---\n\n"

        # ------------------------------------------------------------------
        # Pre-compute all ciphertexts outside the hot loop so that only the
        # decrypt operation itself is timed in the decrypt block below.
        # ------------------------------------------------------------------
        iodine_ct_chacha  = Iodine::Base::Crypto::X25519.encrypt(plaintext, recipient_pk: recipient_pk)
        iodine_ct_aes128  = Iodine::Base::Crypto::X25519.encrypt_aes128(plaintext, recipient_pk: recipient_pk)
        iodine_ct_aes256  = Iodine::Base::Crypto::X25519.encrypt_aes256(plaintext, recipient_pk: recipient_pk)
        rbnacl_ciphertext = rbnacl_available ? RbNaCl::SimpleBox.from_public_key(rbnacl_pk).encrypt(plaintext) : nil

        # ------------------------------------------------------------------
        # Unified encrypt block — all cipher variants compete side-by-side.
        # x.compare! prints a ranked table so the fastest cipher is clear.
        # ------------------------------------------------------------------
        puts "Encrypt — all cipher variants ranked:"
        ::Benchmark.ips do |x|
          x.report('Iodine X25519.encrypt (ChaCha20)'.ljust(lw)) do
            Iodine::Base::Crypto::X25519.encrypt(plaintext, recipient_pk: recipient_pk)
          end
          x.report('Iodine X25519.encrypt_aes128'.ljust(lw)) do
            Iodine::Base::Crypto::X25519.encrypt_aes128(plaintext, recipient_pk: recipient_pk)
          end
          x.report('Iodine X25519.encrypt_aes256'.ljust(lw)) do
            Iodine::Base::Crypto::X25519.encrypt_aes256(plaintext, recipient_pk: recipient_pk)
          end
          if rbnacl_available
            # RbNaCl SimpleBox uses XSalsa20-Poly1305 — different cipher, shown for reference
            x.report('RbNaCl SimpleBox seal'.ljust(lw)) do
              RbNaCl::SimpleBox.from_public_key(rbnacl_pk).encrypt(plaintext)
            end
          end
          x.compare!
        end

        # ------------------------------------------------------------------
        # Unified decrypt block — all cipher variants compete side-by-side.
        # Ciphertexts were pre-computed above (outside the hot loop).
        # x.compare! prints a ranked table so the fastest cipher is clear.
        # ------------------------------------------------------------------
        puts "\nDecrypt — all cipher variants ranked:"
        ::Benchmark.ips do |x|
          x.report('Iodine X25519.decrypt (ChaCha20)'.ljust(lw)) do
            Iodine::Base::Crypto::X25519.decrypt(iodine_ct_chacha, secret_key: recipient_sk)
          end
          x.report('Iodine X25519.decrypt_aes128'.ljust(lw)) do
            Iodine::Base::Crypto::X25519.decrypt_aes128(iodine_ct_aes128, secret_key: recipient_sk)
          end
          x.report('Iodine X25519.decrypt_aes256'.ljust(lw)) do
            Iodine::Base::Crypto::X25519.decrypt_aes256(iodine_ct_aes256, secret_key: recipient_sk)
          end
          if rbnacl_available
            # RbNaCl SimpleBox uses XSalsa20-Poly1305 — different cipher, shown for reference
            x.report('RbNaCl SimpleBox open'.ljust(lw)) do
              RbNaCl::SimpleBox.from_keypair(rbnacl_pk, rbnacl_sk).decrypt(rbnacl_ciphertext)
            end
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
      ikm  = Iodine::Utils.random(32)
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
    #
    # FAIRNESS NOTES:
    # - Both implementations use the SAME underlying secret bytes. Iodine
    #   accepts raw bytes; ROTP accepts Base32-encoded strings. The Base32
    #   encoding of the raw secret is decoded by ROTP back to the same bytes,
    #   so both implementations compute TOTP for the same secret.
    # - Verification windows are matched: Iodine window:1 checks ±1 interval;
    #   ROTP drift_behind/drift_ahead:30 also checks ±1 interval (30 seconds).
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

      # Generate a raw 20-byte secret (the canonical TOTP secret).
      # Iodine::Utils.totp expects raw bytes.
      # ROTP expects a Base32-encoded string — we use the Base32 encoding of
      # the same raw bytes so both implementations operate on the same secret.
      raw_secret = Iodine::Utils.random(20)

      # totp_secret returns a Base32-encoded string from random bytes.
      # We need the Base32 encoding of our specific raw_secret for ROTP.
      # Use Iodine's own Base32 encoder via totp_secret if possible, otherwise
      # fall back to ROTP's Base32 encoder.
      rotp_available = defined?(ROTP)

      if rotp_available
        # Encode raw_secret to Base32 for ROTP
        base32_secret = ROTP::Base32.encode(raw_secret)
        rotp_totp = ROTP::TOTP.new(base32_secret)
      end

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

      puts "\nTOTP Code Generation (same secret, same time window):"
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

      # Verification: use the same window size for both.
      # Iodine window:1 = check current ± 1 interval (3 codes total).
      # ROTP drift_behind:30, drift_ahead:30 = check current ± 1 interval (3 codes total).
      current_code = Iodine::Utils.totp(secret: raw_secret).to_i

      puts "\nTOTP Verification (window: ±1 interval = 3 codes checked):"
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

    # Benchmarks Iodine's compression vs Ruby's Zlib stdlib.
    #
    # Tests Deflate, Gzip, and Brotli compression/decompression with
    # various payload sizes and compressible content.
    #
    # FAIRNESS NOTES:
    # - Both Iodine and Zlib use the same compression level (6 = default).
    # - Payloads are compressible text (repeated English prose), not random
    #   bytes. Random bytes are incompressible and would make compression
    #   benchmarks meaningless.
    # - Brotli has no Ruby stdlib equivalent; it is shown solo with a note.
    # - Zlib::Deflate.deflate and Zlib::GzipWriter are the idiomatic one-shot
    #   Ruby APIs and are used here.
    def self.compression
      require 'benchmark/ips'
      require 'zlib'

      puts '=' * 70
      puts 'Compression Benchmarks'
      puts '=' * 70
      puts
      puts 'NOTE: Payloads are compressible English text (not random bytes).'
      puts 'Both Iodine and Zlib use compression level 6 (default).'
      puts

      # Compressible payload: repeated English prose
      prose = "The quick brown fox jumps over the lazy dog. " \
              "Pack my box with five dozen liquor jugs. " \
              "How vexingly quick daft zebras jump! " \
              "The five boxing wizards jump quickly. "

      sizes = {
        '1KB'  => 1024,
        '16KB' => 16_384,
        '64KB' => 65_536
      }

      # Label width — wide enough for 'Iodine Brotli.compress (no Zlib equiv)'
      lw = 30

      sizes.each do |size_name, size|
        # Build a compressible payload of the target size
        plaintext = (prose * ((size / prose.bytesize) + 1))[0, size]
        plaintext.force_encoding(Encoding::BINARY)

        puts "\n--- Payload Size: #{size_name} (compressible text) ---\n\n"

        # ------------------------------------------------------------------
        # Pre-compute all compressed inputs outside the hot loop so that
        # decompression benchmarks only measure decompression, not compression.
        # ------------------------------------------------------------------
        iodine_deflated = Iodine::Base::Compression::Deflate.compress(plaintext, level: 6)
        zlib_deflated   = Zlib::Deflate.deflate(plaintext, Zlib::DEFAULT_COMPRESSION)

        iodine_gzipped = Iodine::Base::Compression::Gzip.compress(plaintext, level: 6)
        zlib_gzipped   = begin
          sio = StringIO.new(String.new, 'wb')
          gz = Zlib::GzipWriter.new(sio, Zlib::DEFAULT_COMPRESSION)
          gz.write(plaintext)
          gz.close
          sio.string
        end

        iodine_brotli = Iodine::Base::Compression::Brotli.compress(plaintext, quality: 4)

        # ------------------------------------------------------------------
        # Unified compress block — all algorithms compete side-by-side.
        # Brotli has no Zlib equivalent but is included for ranking context.
        # x.compare! prints a ranked table so the fastest algorithm is clear.
        # ------------------------------------------------------------------
        puts "Compress — all algorithms ranked (level 6 / quality 4):"
        puts "  (Brotli has no Zlib equivalent — included for ranking context)"
        ::Benchmark.ips do |x|
          x.report('Iodine Deflate.compress'.ljust(lw)) do
            Iodine::Base::Compression::Deflate.compress(plaintext, level: 6)
          end
          x.report('Zlib::Deflate.deflate'.ljust(lw)) do
            Zlib::Deflate.deflate(plaintext, Zlib::DEFAULT_COMPRESSION)
          end
          x.report('Iodine Gzip.compress'.ljust(lw)) do
            Iodine::Base::Compression::Gzip.compress(plaintext, level: 6)
          end
          x.report('Zlib gzip (StringIO)'.ljust(lw)) do
            sio = StringIO.new(String.new, 'wb')
            gz = Zlib::GzipWriter.new(sio, Zlib::DEFAULT_COMPRESSION)
            gz.write(plaintext)
            gz.close
            sio.string
          end
          # Brotli has no Zlib equivalent — included for throughput ranking context
          x.report('Iodine Brotli.compress (no Zlib equiv)'.ljust(lw)) do
            Iodine::Base::Compression::Brotli.compress(plaintext, quality: 4)
          end
          x.compare!
        end

        # ------------------------------------------------------------------
        # Unified decompress block — all algorithms compete side-by-side.
        # Each implementation decompresses its own format (fair comparison).
        # Brotli has no Zlib equivalent but is included for ranking context.
        # ------------------------------------------------------------------
        puts "\nDecompress — all algorithms ranked:"
        puts "  (Brotli has no Zlib equivalent — included for ranking context)"
        ::Benchmark.ips do |x|
          x.report('Iodine Deflate.decompress'.ljust(lw)) do
            Iodine::Base::Compression::Deflate.decompress(iodine_deflated)
          end
          x.report('Zlib::Inflate.inflate'.ljust(lw)) do
            Zlib::Inflate.inflate(zlib_deflated)
          end
          x.report('Iodine Gzip.decompress'.ljust(lw)) do
            Iodine::Base::Compression::Gzip.decompress(iodine_gzipped)
          end
          x.report('Zlib::GzipReader'.ljust(lw)) do
            Zlib::GzipReader.new(StringIO.new(zlib_gzipped)).read
          end
          # Brotli has no Zlib equivalent — included for throughput ranking context
          x.report('Iodine Brotli.decompress (no Zlib equiv)'.ljust(lw)) do
            Iodine::Base::Compression::Brotli.decompress(iodine_brotli)
          end
          x.compare!
        end
      end

      nil
    end

    # Benchmarks Iodine's non-cryptographic hash functions.
    #
    # Tests risky_hash (64-bit), risky256, risky512, and crc32 against
    # Ruby stdlib equivalents where available.
    #
    # FAIRNESS NOTES:
    # - risky_hash and String#hash serve the same purpose: seeding hash maps.
    #   Ruby's Hash tables use String#hash (SipHash, seeded per-process).
    #   Iodine's MiniMap uses risky_hash — a faster alternative to SipHash with
    #   proven collision resistance and superior bit distribution.
    # - crc32 is compared against Zlib.crc32 (same polynomial: ITU-T V.42).
    # - risky256/risky512 have no stdlib equivalents; shown for reference only.
    def self.non_crypto_hashing
      require 'benchmark/ips'
      require 'zlib'

      puts '=' * 70
      puts 'Non-Cryptographic Hash Benchmarks'
      puts '=' * 70
      puts
      puts 'NOTE: risky_hash and String#hash both seed hash maps — a direct'
      puts 'comparison. Ruby Hash uses String#hash (SipHash, per-process seed).'
      puts 'Iodine MiniMap uses risky_hash: faster than SipHash with proven'
      puts 'collision resistance and superior bit distribution.'
      puts

      sizes = {
        '64B'  => 64,
        '1KB'  => 1024,
        '64KB' => 65_536
      }

      # Label width — wide enough for 'Iodine risky512 (non-crypto, 64B)'
      lw = 32

      sizes.each do |size_name, size|
        data = Iodine::Utils.random(size)

        puts "\n--- Data Size: #{size_name} — all non-crypto hashes ranked together ---\n\n"
        puts "  (risky256/risky512 have no stdlib equivalent; String#hash uses SipHash — same use case as risky_hash)"
        puts

        # ------------------------------------------------------------------
        # Unified block: all non-crypto hash algorithms compete side-by-side.
        # x.compare! prints a ranked table so the fastest algorithm is clear.
        # ------------------------------------------------------------------
        ::Benchmark.ips do |x|
          x.report('Iodine risky_hash (64-bit)'.ljust(lw)) do
            Iodine::Utils.risky_hash(data)
          end
          x.report('Iodine crc32 (32-bit)'.ljust(lw)) do
            Iodine::Utils.crc32(data)
          end
          # risky256/risky512 have no stdlib equivalent — included for ranking context
          x.report('Iodine risky256 (non-crypto, 32B)'.ljust(lw)) do
            Iodine::Utils.risky256(data)
          end
          x.report('Iodine risky512 (non-crypto, 64B)'.ljust(lw)) do
            Iodine::Utils.risky512(data)
          end
          # Zlib.crc32 — direct equivalent for crc32 (same ITU-T V.42 polynomial)
          x.report('Zlib.crc32'.ljust(lw)) do
            Zlib.crc32(data)
          end
          # String#hash uses SipHash (seeded per-process) — same use case as
          # risky_hash: both are hash-map hash functions. Direct comparison.
          x.report('String#hash (SipHash, per-process)'.ljust(lw)) do
            data.hash
          end
          x.compare!
        end
      end

      nil
    end

    # Benchmarks Iodine's X25519+ML-KEM-768 post-quantum hybrid KEM.
    #
    # X25519MLKEM768 is a hybrid Key Encapsulation Mechanism that combines:
    # - Classical X25519 elliptic curve Diffie-Hellman
    # - ML-KEM-768 (formerly Kyber), a post-quantum lattice-based KEM
    #
    # The hybrid construction provides security against both classical and
    # quantum computer attacks. Even if one algorithm is broken, the other
    # still provides security.
    #
    # Key sizes:
    # - Secret key: 2432 bytes  |  Public key: 1216 bytes
    # - Ciphertext: 1120 bytes  |  Shared secret: 64 bytes
    #
    # There is no Ruby stdlib or OpenSSL equivalent for ML-KEM-768.
    # X25519.keypair is included for relative cost comparison only.
    #
    # FAIRNESS NOTES:
    # - Keypairs are generated OUTSIDE the encapsulate/decapsulate hot loops.
    # - X25519.keypair is shown alongside X25519MLKEM768.keypair so readers
    #   can see the overhead of adding ML-KEM-768 to a classical X25519 key.
    # - No compare! for solo benchmarks (no stdlib equivalent).
    def self.crypto_pqkem
      require 'benchmark/ips'

      puts '=' * 70
      puts 'Post-Quantum Hybrid KEM Benchmarks (X25519 + ML-KEM-768)'
      puts '=' * 70
      puts
      puts 'NOTE: X25519MLKEM768 is a hybrid KEM combining classical X25519 with'
      puts 'ML-KEM-768 (Kyber). No Ruby stdlib or OpenSSL equivalent exists.'
      puts 'X25519.keypair is shown for relative cost comparison only.'
      puts

      # ------------------------------------------------------------------
      # Key generation — compare X25519MLKEM768 vs plain X25519
      # ------------------------------------------------------------------
      puts 'Key Generation (X25519MLKEM768 vs X25519 for relative cost):'
      ::Benchmark.ips do |x|
        x.report('Iodine X25519MLKEM768.keypair') do
          Iodine::Base::Crypto::X25519MLKEM768.keypair
        end

        # X25519 keypair shown for relative cost reference only.
        # X25519MLKEM768 is expected to be significantly slower due to ML-KEM-768.
        x.report('Iodine X25519.keypair (classical, for reference)') do
          Iodine::Base::Crypto::X25519.keypair
        end

        x.compare!
      end

      # Generate a keypair outside the hot loop for encapsulate/decapsulate
      pqkem_sk, pqkem_pk = Iodine::Base::Crypto::X25519MLKEM768.keypair

      # ------------------------------------------------------------------
      # Encapsulation — sender generates shared secret + ciphertext
      #
      # No stdlib equivalent; shown for throughput reference only.
      # ------------------------------------------------------------------
      puts "\nEncapsulation (sender generates shared secret + 1120-byte ciphertext):"
      puts "  [No stdlib equivalent — shown for throughput reference only]"
      ::Benchmark.ips do |x|
        x.report('Iodine X25519MLKEM768.encapsulate') do
          Iodine::Base::Crypto::X25519MLKEM768.encapsulate(public_key: pqkem_pk)
        end
        # No compare! — no stdlib equivalent
      end

      # Generate a ciphertext outside the hot loop for decapsulate
      pqkem_ct, = Iodine::Base::Crypto::X25519MLKEM768.encapsulate(public_key: pqkem_pk)

      # ------------------------------------------------------------------
      # Decapsulation — recipient recovers shared secret from ciphertext
      #
      # No stdlib equivalent; shown for throughput reference only.
      # ------------------------------------------------------------------
      puts "\nDecapsulation (recipient recovers 64-byte shared secret from ciphertext):"
      puts "  [No stdlib equivalent — shown for throughput reference only]"
      ::Benchmark.ips do |x|
        x.report('Iodine X25519MLKEM768.decapsulate') do
          Iodine::Base::Crypto::X25519MLKEM768.decapsulate(ciphertext: pqkem_ct, secret_key: pqkem_sk)
        end
        # No compare! — no stdlib equivalent
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
    # - compression (Deflate, Gzip, Brotli)
    # - non_crypto_hashing (risky_hash, crc32, risky256, risky512)
    # - crypto_pqkem (X25519+ML-KEM-768 post-quantum hybrid KEM)
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
      puts "\n"
      compression
      puts "\n"
      non_crypto_hashing
      puts "\n"
      crypto_pqkem

      puts "\n" + '=' * 70
      puts 'All Crypto Benchmarks Complete'
      puts '=' * 70

      nil
    end
  end
end
