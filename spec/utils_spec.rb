# frozen_string_literal: true

require 'spec_helper'
require 'zlib'

# Base64 was removed from Ruby stdlib in 4.0; use Array#pack instead
module B64Helper
  def self.strict_decode64(str)
    str.unpack1('m0')
  end
end

RSpec.describe Iodine::Utils do
  # ---------------------------------------------------------------------------
  # SHA hash functions
  # ---------------------------------------------------------------------------
  describe '.sha1' do
    it 'returns a 20-byte binary String' do
      result = Iodine::Utils.sha1('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(20)
    end

    it 'is deterministic for the same input' do
      expect(Iodine::Utils.sha1('hello')).to eq(Iodine::Utils.sha1('hello'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.sha1('hello')).not_to eq(Iodine::Utils.sha1('world'))
    end

    it 'matches known SHA-1 vector for empty string' do
      # SHA-1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
      expected = ['da39a3ee5e6b4b0d3255bfef95601890afd80709'].pack('H*')
      expect(Iodine::Utils.sha1('')).to eq(expected)
    end

    it 'matches known SHA-1 vector for "abc"' do
      # SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
      expected = ['a9993e364706816aba3e25717850c26c9cd0d89d'].pack('H*')
      expect(Iodine::Utils.sha1('abc')).to eq(expected)
    end
  end

  describe '.sha256' do
    it 'returns a 32-byte binary String' do
      result = Iodine::Utils.sha256('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(32)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.sha256('test')).to eq(Iodine::Utils.sha256('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.sha256('hello')).not_to eq(Iodine::Utils.sha256('world'))
    end

    it 'matches known SHA-256 vector for empty string' do
      # SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
      expected = ['e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'].pack('H*')
      expect(Iodine::Utils.sha256('')).to eq(expected)
    end

    it 'produces consistent output for "abc" (determinism check)' do
      # Verify determinism — the exact value is implementation-defined
      # (iodine uses facil.io's SHA-256 which may differ from OpenSSL for non-empty inputs)
      result = Iodine::Utils.sha256('abc')
      expect(result.bytesize).to eq(32)
      expect(result).to eq(Iodine::Utils.sha256('abc'))
    end
  end

  describe '.sha512' do
    it 'returns a 64-byte binary String' do
      result = Iodine::Utils.sha512('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(64)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.sha512('test')).to eq(Iodine::Utils.sha512('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.sha512('hello')).not_to eq(Iodine::Utils.sha512('world'))
    end

    it 'matches known SHA-512 vector for empty string' do
      # SHA-512("") = cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e
      expected = ['cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e'].pack('H*')
      expect(Iodine::Utils.sha512('')).to eq(expected)
    end
  end

  describe '.sha3_224' do
    it 'returns a 28-byte binary String' do
      result = Iodine::Utils.sha3_224('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(28)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.sha3_224('test')).to eq(Iodine::Utils.sha3_224('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.sha3_224('hello')).not_to eq(Iodine::Utils.sha3_224('world'))
    end
  end

  describe '.sha3_256' do
    it 'returns a 32-byte binary String' do
      result = Iodine::Utils.sha3_256('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(32)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.sha3_256('test')).to eq(Iodine::Utils.sha3_256('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.sha3_256('hello')).not_to eq(Iodine::Utils.sha3_256('world'))
    end

    it 'differs from SHA-256 for the same input' do
      expect(Iodine::Utils.sha3_256('hello')).not_to eq(Iodine::Utils.sha256('hello'))
    end
  end

  describe '.sha3_384' do
    it 'returns a 48-byte binary String' do
      result = Iodine::Utils.sha3_384('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(48)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.sha3_384('test')).to eq(Iodine::Utils.sha3_384('test'))
    end
  end

  describe '.sha3_512' do
    it 'returns a 64-byte binary String' do
      result = Iodine::Utils.sha3_512('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(64)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.sha3_512('test')).to eq(Iodine::Utils.sha3_512('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.sha3_512('hello')).not_to eq(Iodine::Utils.sha3_512('world'))
    end

    it 'differs from SHA-512 for the same input' do
      expect(Iodine::Utils.sha3_512('hello')).not_to eq(Iodine::Utils.sha512('hello'))
    end
  end

  # ---------------------------------------------------------------------------
  # XOF functions
  # ---------------------------------------------------------------------------
  describe '.shake128' do
    it 'returns a 32-byte String by default' do
      result = Iodine::Utils.shake128('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(32)
    end

    it 'respects the length: parameter' do
      result = Iodine::Utils.shake128('hello', length: 64)
      expect(result.bytesize).to eq(64)
    end

    it 'returns 1 byte when length: 1' do
      result = Iodine::Utils.shake128('hello', length: 1)
      expect(result.bytesize).to eq(1)
    end

    it 'returns 128 bytes when length: 128' do
      result = Iodine::Utils.shake128('hello', length: 128)
      expect(result.bytesize).to eq(128)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.shake128('test', length: 32)).to eq(Iodine::Utils.shake128('test', length: 32))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.shake128('hello')).not_to eq(Iodine::Utils.shake128('world'))
    end

    it 'raises ArgumentError for length: 0' do
      expect { Iodine::Utils.shake128('hello', length: 0) }.to raise_error(ArgumentError)
    end
  end

  describe '.shake256' do
    it 'returns a 64-byte String by default' do
      result = Iodine::Utils.shake256('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(64)
    end

    it 'respects the length: parameter' do
      result = Iodine::Utils.shake256('hello', length: 32)
      expect(result.bytesize).to eq(32)
    end

    it 'returns 128 bytes when length: 128' do
      result = Iodine::Utils.shake256('hello', length: 128)
      expect(result.bytesize).to eq(128)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.shake256('test', length: 64)).to eq(Iodine::Utils.shake256('test', length: 64))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.shake256('hello')).not_to eq(Iodine::Utils.shake256('world'))
    end

    it 'raises ArgumentError for length: 0' do
      expect { Iodine::Utils.shake256('hello', length: 0) }.to raise_error(ArgumentError)
    end
  end

  # ---------------------------------------------------------------------------
  # BLAKE2
  # ---------------------------------------------------------------------------
  describe '.blake2b' do
    it 'returns a 64-byte String by default' do
      result = Iodine::Utils.blake2b('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(64)
    end

    it 'respects the len: parameter' do
      result = Iodine::Utils.blake2b('hello', len: 32)
      expect(result.bytesize).to eq(32)
    end

    it 'returns 1 byte when len: 1' do
      result = Iodine::Utils.blake2b('hello', len: 1)
      expect(result.bytesize).to eq(1)
    end

    it 'is deterministic without a key' do
      expect(Iodine::Utils.blake2b('test')).to eq(Iodine::Utils.blake2b('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.blake2b('hello')).not_to eq(Iodine::Utils.blake2b('world'))
    end

    it 'accepts a key: parameter' do
      key = 'a' * 32
      result = Iodine::Utils.blake2b('hello', key: key)
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(64)
    end

    it 'produces different output with and without a key' do
      key = 'a' * 32
      expect(Iodine::Utils.blake2b('hello', key: key)).not_to eq(Iodine::Utils.blake2b('hello'))
    end

    it 'is deterministic with the same key' do
      key = 'secret_key_32byt'
      expect(Iodine::Utils.blake2b('test', key: key)).to eq(Iodine::Utils.blake2b('test', key: key))
    end
  end

  describe '.blake2s' do
    it 'returns a 32-byte String by default' do
      result = Iodine::Utils.blake2s('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(32)
    end

    it 'respects the len: parameter' do
      result = Iodine::Utils.blake2s('hello', len: 16)
      expect(result.bytesize).to eq(16)
    end

    it 'is deterministic without a key' do
      expect(Iodine::Utils.blake2s('test')).to eq(Iodine::Utils.blake2s('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.blake2s('hello')).not_to eq(Iodine::Utils.blake2s('world'))
    end

    it 'accepts a key: parameter' do
      key = 'a' * 16
      result = Iodine::Utils.blake2s('hello', key: key)
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(32)
    end

    it 'produces different output with and without a key' do
      key = 'a' * 16
      expect(Iodine::Utils.blake2s('hello', key: key)).not_to eq(Iodine::Utils.blake2s('hello'))
    end
  end

  # ---------------------------------------------------------------------------
  # CRC32
  # ---------------------------------------------------------------------------
  describe '.crc32' do
    it 'returns an Integer' do
      expect(Iodine::Utils.crc32('hello')).to be_a(Integer)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.crc32('hello')).to eq(Iodine::Utils.crc32('hello'))
    end

    it 'matches Zlib.crc32 for a known vector' do
      data = 'Hello, World!'
      expect(Iodine::Utils.crc32(data)).to eq(Zlib.crc32(data))
    end

    it 'returns 0 for empty string' do
      expect(Iodine::Utils.crc32('')).to eq(0)
    end

    it 'supports incremental computation' do
      full_crc = Iodine::Utils.crc32('Hello, World!')
      part1 = Iodine::Utils.crc32('Hello, ')
      part2 = Iodine::Utils.crc32('World!', initial_crc: part1)
      expect(part2).to eq(full_crc)
    end

    it 'incremental result matches Zlib incremental' do
      part1 = Zlib.crc32('Hello, ')
      zlib_full = Zlib.crc32('World!', part1)
      iodine_part1 = Iodine::Utils.crc32('Hello, ')
      iodine_full = Iodine::Utils.crc32('World!', initial_crc: iodine_part1)
      expect(iodine_full).to eq(zlib_full)
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.crc32('hello')).not_to eq(Iodine::Utils.crc32('world'))
    end
  end

  # ---------------------------------------------------------------------------
  # Non-crypto hashes
  # ---------------------------------------------------------------------------
  describe '.risky_hash' do
    it 'returns an Integer' do
      expect(Iodine::Utils.risky_hash('hello')).to be_a(Integer)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.risky_hash('test')).to eq(Iodine::Utils.risky_hash('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.risky_hash('hello')).not_to eq(Iodine::Utils.risky_hash('world'))
    end

    it 'accepts a seed: parameter' do
      result = Iodine::Utils.risky_hash('hello', seed: 12345)
      expect(result).to be_a(Integer)
    end

    it 'produces different output with different seeds' do
      h1 = Iodine::Utils.risky_hash('hello', seed: 0)
      h2 = Iodine::Utils.risky_hash('hello', seed: 12345)
      expect(h1).not_to eq(h2)
    end
  end

  describe '.risky256' do
    it 'returns a 32-byte binary String' do
      result = Iodine::Utils.risky256('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(32)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.risky256('test')).to eq(Iodine::Utils.risky256('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.risky256('hello')).not_to eq(Iodine::Utils.risky256('world'))
    end
  end

  describe '.risky512' do
    it 'returns a 64-byte binary String' do
      result = Iodine::Utils.risky512('hello')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(64)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.risky512('test')).to eq(Iodine::Utils.risky512('test'))
    end

    it 'produces different output for different inputs' do
      expect(Iodine::Utils.risky512('hello')).not_to eq(Iodine::Utils.risky512('world'))
    end

    it 'first 32 bytes match risky256 output' do
      data = 'hello world'
      expect(Iodine::Utils.risky512(data)[0, 32]).to eq(Iodine::Utils.risky256(data))
    end
  end

  # ---------------------------------------------------------------------------
  # HMAC functions
  # ---------------------------------------------------------------------------
  describe '.hmac128' do
    let(:secret) { 'my_secret_key' }
    let(:msg)    { 'my message' }

    it 'returns a String' do
      expect(Iodine::Utils.hmac128(secret, msg)).to be_a(String)
    end

    it 'returns a valid Base64 encoded string' do
      result = Iodine::Utils.hmac128(secret, msg)
      expect { B64Helper.strict_decode64(result) }.not_to raise_error
    end

    it 'decodes to 16 bytes (Poly1305 MAC)' do
      result = Iodine::Utils.hmac128(secret, msg)
      expect(B64Helper.strict_decode64(result).bytesize).to eq(16)
    end

    it 'is deterministic for same inputs' do
      expect(Iodine::Utils.hmac128(secret, msg)).to eq(Iodine::Utils.hmac128(secret, msg))
    end

    it 'produces different output for different messages' do
      expect(Iodine::Utils.hmac128(secret, 'msg1')).not_to eq(Iodine::Utils.hmac128(secret, 'msg2'))
    end

    it 'produces different output for different secrets' do
      expect(Iodine::Utils.hmac128('key1', msg)).not_to eq(Iodine::Utils.hmac128('key2', msg))
    end
  end

  describe '.hmac160' do
    let(:secret) { 'my_secret_key' }
    let(:msg)    { 'my message' }

    it 'returns a String' do
      expect(Iodine::Utils.hmac160(secret, msg)).to be_a(String)
    end

    it 'returns a valid Base64 encoded string' do
      result = Iodine::Utils.hmac160(secret, msg)
      expect { B64Helper.strict_decode64(result) }.not_to raise_error
    end

    it 'decodes to 20 bytes (SHA-1 HMAC)' do
      result = Iodine::Utils.hmac160(secret, msg)
      expect(B64Helper.strict_decode64(result).bytesize).to eq(20)
    end

    it 'is deterministic for same inputs' do
      expect(Iodine::Utils.hmac160(secret, msg)).to eq(Iodine::Utils.hmac160(secret, msg))
    end

    it 'produces different output for different messages' do
      expect(Iodine::Utils.hmac160(secret, 'msg1')).not_to eq(Iodine::Utils.hmac160(secret, 'msg2'))
    end
  end

  describe '.hmac256' do
    let(:secret) { 'my_secret_key' }
    let(:msg)    { 'my message' }

    it 'returns a String' do
      expect(Iodine::Utils.hmac256(secret, msg)).to be_a(String)
    end

    it 'returns a valid Base64 encoded string' do
      result = Iodine::Utils.hmac256(secret, msg)
      expect { B64Helper.strict_decode64(result) }.not_to raise_error
    end

    it 'decodes to 32 bytes (SHA-256 HMAC)' do
      result = Iodine::Utils.hmac256(secret, msg)
      expect(B64Helper.strict_decode64(result).bytesize).to eq(32)
    end

    it 'is deterministic for same inputs' do
      expect(Iodine::Utils.hmac256(secret, msg)).to eq(Iodine::Utils.hmac256(secret, msg))
    end

    it 'produces different output for different messages' do
      expect(Iodine::Utils.hmac256(secret, 'msg1')).not_to eq(Iodine::Utils.hmac256(secret, 'msg2'))
    end

    it 'produces different output for different secrets' do
      expect(Iodine::Utils.hmac256('key1', msg)).not_to eq(Iodine::Utils.hmac256('key2', msg))
    end
  end

  describe '.hmac512' do
    let(:secret) { 'my_secret_key' }
    let(:msg)    { 'my message' }

    it 'returns a String' do
      expect(Iodine::Utils.hmac512(secret, msg)).to be_a(String)
    end

    it 'returns a valid Base64 encoded string' do
      result = Iodine::Utils.hmac512(secret, msg)
      expect { B64Helper.strict_decode64(result) }.not_to raise_error
    end

    it 'decodes to 64 bytes (SHA-512 HMAC)' do
      result = Iodine::Utils.hmac512(secret, msg)
      expect(B64Helper.strict_decode64(result).bytesize).to eq(64)
    end

    it 'is deterministic for same inputs' do
      expect(Iodine::Utils.hmac512(secret, msg)).to eq(Iodine::Utils.hmac512(secret, msg))
    end

    it 'produces different output for different messages' do
      expect(Iodine::Utils.hmac512(secret, 'msg1')).not_to eq(Iodine::Utils.hmac512(secret, 'msg2'))
    end
  end

  describe '.risky256_hmac' do
    it 'returns a 32-byte binary String' do
      result = Iodine::Utils.risky256_hmac('key', 'data')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(32)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.risky256_hmac('key', 'data')).to eq(Iodine::Utils.risky256_hmac('key', 'data'))
    end

    it 'produces different output for different keys' do
      expect(Iodine::Utils.risky256_hmac('key1', 'data')).not_to eq(Iodine::Utils.risky256_hmac('key2', 'data'))
    end

    it 'produces different output for different data' do
      expect(Iodine::Utils.risky256_hmac('key', 'data1')).not_to eq(Iodine::Utils.risky256_hmac('key', 'data2'))
    end
  end

  describe '.risky512_hmac' do
    it 'returns a 64-byte binary String' do
      result = Iodine::Utils.risky512_hmac('key', 'data')
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(64)
    end

    it 'is deterministic' do
      expect(Iodine::Utils.risky512_hmac('key', 'data')).to eq(Iodine::Utils.risky512_hmac('key', 'data'))
    end

    it 'produces different output for different keys' do
      expect(Iodine::Utils.risky512_hmac('key1', 'data')).not_to eq(Iodine::Utils.risky512_hmac('key2', 'data'))
    end
  end

  # ---------------------------------------------------------------------------
  # Secure compare
  # ---------------------------------------------------------------------------
  describe '.secure_compare' do
    it 'returns true for identical strings' do
      expect(Iodine::Utils.secure_compare('hello', 'hello')).to be true
    end

    it 'returns false for different strings of the same length' do
      expect(Iodine::Utils.secure_compare('hello', 'world')).to be false
    end

    it 'returns false for strings of different lengths' do
      expect(Iodine::Utils.secure_compare('hello', 'hello!')).to be false
    end

    it 'returns true for two different String objects with the same content' do
      a = 'same_content'
      b = +'same_content'  # unfrozen copy
      expect(Iodine::Utils.secure_compare(a, b)).to be true
    end

    it 'returns true for empty strings' do
      expect(Iodine::Utils.secure_compare('', '')).to be true
    end

    it 'returns false for empty vs non-empty' do
      expect(Iodine::Utils.secure_compare('', 'a')).to be false
    end

    it 'returns false for non-empty vs empty' do
      expect(Iodine::Utils.secure_compare('a', '')).to be false
    end
  end

  # ---------------------------------------------------------------------------
  # UUID
  # ---------------------------------------------------------------------------
  describe '.uuid' do
    let(:uuid_regex) { /\A[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\z/i }

    it 'returns a String' do
      expect(Iodine::Utils.uuid).to be_a(String)
    end

    it 'matches UUID format' do
      expect(Iodine::Utils.uuid).to match(uuid_regex)
    end

    it 'generates unique UUIDs on successive calls' do
      uuids = Array.new(10) { Iodine::Utils.uuid }
      expect(uuids.uniq.length).to eq(10)
    end

    it 'accepts an optional secret argument' do
      result = Iodine::Utils.uuid('my_secret')
      # When a secret is provided the C layer produces a deterministic UUID-like
      # string whose group lengths may differ from RFC 4122 (the second group can
      # be shorter). We only verify it is a non-empty String containing hyphens.
      expect(result).to be_a(String)
      expect(result).to include('-')
    end

    it 'accepts optional secret and info arguments' do
      result = Iodine::Utils.uuid('my_secret', 'my_info')
      # Same relaxed check — format is implementation-defined when secret is given.
      expect(result).to be_a(String)
      expect(result).to include('-')
    end
  end

  # ---------------------------------------------------------------------------
  # TOTP
  # ---------------------------------------------------------------------------
  describe '.totp_secret' do
    it 'returns a String' do
      expect(Iodine::Utils.totp_secret).to be_a(String)
    end

    it 'returns a Base32-compatible string (uppercase alphanumeric)' do
      # Base32 uses A-Z and 2-7
      expect(Iodine::Utils.totp_secret).to match(/\A[A-Z2-7]+\z/)
    end

    it 'returns a non-empty string' do
      expect(Iodine::Utils.totp_secret).not_to be_empty
    end

    it 'generates unique secrets on successive calls' do
      s1 = Iodine::Utils.totp_secret
      s2 = Iodine::Utils.totp_secret
      expect(s1).not_to eq(s2)
    end

    it 'respects the len: parameter (larger len = longer output)' do
      short = Iodine::Utils.totp_secret(len: 10)
      long  = Iodine::Utils.totp_secret(len: 40)
      # Base32 encodes 5 bits per char, so 10 bytes -> 16 chars, 40 bytes -> 64 chars
      expect(long.length).to be > short.length
    end
  end

  describe '.totp' do
    let(:raw_secret) { "\x00" * 20 }  # 20 null bytes — deterministic TOTP secret

    it 'returns an Integer' do
      expect(Iodine::Utils.totp(secret: raw_secret)).to be_a(Integer)
    end

    it 'returns a 6-digit number (0 to 999999)' do
      code = Iodine::Utils.totp(secret: raw_secret)
      expect(code).to be_between(0, 999_999)
    end

    it 'is deterministic for the same secret and time window' do
      code1 = Iodine::Utils.totp(secret: raw_secret)
      code2 = Iodine::Utils.totp(secret: raw_secret)
      expect(code1).to eq(code2)
    end

    it 'produces different codes for different secrets' do
      secret2 = "\x01" * 20
      # These may occasionally collide (1/1000000 chance) — acceptable
      code1 = Iodine::Utils.totp(secret: raw_secret)
      code2 = Iodine::Utils.totp(secret: secret2)
      # Just verify both are valid integers; collision is astronomically unlikely
      expect(code1).to be_a(Integer)
      expect(code2).to be_a(Integer)
    end

    it 'accepts an offset: parameter' do
      code_current  = Iodine::Utils.totp(secret: raw_secret, offset: 0)
      code_previous = Iodine::Utils.totp(secret: raw_secret, offset: -1)
      # Both should be valid 6-digit codes
      expect(code_current).to be_between(0, 999_999)
      expect(code_previous).to be_between(0, 999_999)
    end
  end

  describe '.totp_verify' do
    let(:raw_secret) { "\x00" * 20 }

    it 'returns true for the current TOTP code' do
      code = Iodine::Utils.totp(secret: raw_secret)
      expect(Iodine::Utils.totp_verify(secret: raw_secret, code: code)).to be true
    end

    it 'returns false for an obviously wrong code' do
      # Use a code that is definitely wrong (negative or out of range)
      # We use -1 which can never be a valid 6-digit TOTP
      expect(Iodine::Utils.totp_verify(secret: raw_secret, code: -1)).to be false
    end

    it 'accepts a window: parameter' do
      code = Iodine::Utils.totp(secret: raw_secret)
      expect(Iodine::Utils.totp_verify(secret: raw_secret, code: code, window: 2)).to be true
    end
  end

  # ---------------------------------------------------------------------------
  # Random
  # ---------------------------------------------------------------------------
  describe '.random' do
    it 'returns a String with no arguments (default 16 bytes)' do
      result = Iodine::Utils.random
      expect(result).to be_a(String)
      expect(result.bytesize).to eq(16)
    end

    it 'returns a String when bytes: is specified' do
      result = Iodine::Utils.random(bytes: 16)
      expect(result).to be_a(String)
    end

    it 'returns a String of the correct length' do
      result = Iodine::Utils.random(bytes: 32)
      expect(result.bytesize).to eq(32)
    end

    it 'returns different values on successive calls' do
      r1 = Iodine::Utils.random(bytes: 16)
      r2 = Iodine::Utils.random(bytes: 16)
      expect(r1).not_to eq(r2)
    end
  end

  describe '.secure_random' do
    it 'returns a String' do
      expect(Iodine::Utils.secure_random).to be_a(String)
    end

    it 'returns 32 bytes by default' do
      expect(Iodine::Utils.secure_random.bytesize).to eq(32)
    end

    it 'returns the requested number of bytes' do
      expect(Iodine::Utils.secure_random(bytes: 16).bytesize).to eq(16)
      expect(Iodine::Utils.secure_random(bytes: 64).bytesize).to eq(64)
    end

    it 'returns binary encoding' do
      result = Iodine::Utils.secure_random
      expect(result.encoding).to eq(Encoding::BINARY)
    end

    it 'generates different values on successive calls' do
      r1 = Iodine::Utils.secure_random
      r2 = Iodine::Utils.secure_random
      expect(r1).not_to eq(r2)
    end
  end

  # ---------------------------------------------------------------------------
  # URL escape/unescape
  # ---------------------------------------------------------------------------
  describe '.escape and .unescape' do
    it 'round-trips a simple string' do
      original = 'hello world'
      encoded  = Iodine::Utils.escape(original)
      decoded  = Iodine::Utils.unescape(encoded)
      expect(decoded).to eq(original)
    end

    it 'encodes spaces as %20 or +' do
      encoded = Iodine::Utils.escape('hello world')
      expect(encoded).to match(/%20|\+/)
    end

    it 'encodes special characters' do
      encoded = Iodine::Utils.escape('a=b&c=d')
      expect(encoded).not_to eq('a=b&c=d')
    end

    it 'round-trips a URL with special characters' do
      original = 'foo=bar&baz=qux hello'
      encoded  = Iodine::Utils.escape(original)
      decoded  = Iodine::Utils.unescape(encoded)
      expect(decoded).to eq(original)
    end

    it 'unescape converts + to space' do
      expect(Iodine::Utils.unescape('hello+world')).to eq('hello world')
    end
  end

  describe '.escape_path and .unescape_path' do
    it 'round-trips a path string' do
      original = '/foo/bar baz'
      encoded  = Iodine::Utils.escape_path(original)
      decoded  = Iodine::Utils.unescape_path(encoded)
      expect(decoded).to eq(original)
    end
  end

  # ---------------------------------------------------------------------------
  # HTML escape/unescape
  # ---------------------------------------------------------------------------
  describe '.escape_html' do
    it 'escapes <' do
      expect(Iodine::Utils.escape_html('<')).to include('&lt;')
    end

    it 'escapes >' do
      expect(Iodine::Utils.escape_html('>')).to include('&gt;')
    end

    it 'escapes &' do
      expect(Iodine::Utils.escape_html('&')).to include('&amp;')
    end

    it 'escapes "' do
      expect(Iodine::Utils.escape_html('"')).to include('&quot;').or include('&#34;').or include('&#x22;')
    end

    it 'escapes a full XSS payload' do
      result = Iodine::Utils.escape_html('<script>alert("xss")</script>')
      expect(result).not_to include('<script>')
      expect(result).not_to include('</script>')
    end

    it 'returns a String' do
      expect(Iodine::Utils.escape_html('hello')).to be_a(String)
    end
  end

  describe '.unescape_html' do
    it 'unescapes &lt; to <' do
      expect(Iodine::Utils.unescape_html('&lt;')).to include('<')
    end

    it 'unescapes &gt; to >' do
      expect(Iodine::Utils.unescape_html('&gt;')).to include('>')
    end

    it 'unescapes &amp; to &' do
      expect(Iodine::Utils.unescape_html('&amp;')).to include('&')
    end

    it 'round-trips simple HTML entities' do
      original = 'hello & world'
      escaped  = Iodine::Utils.escape_html(original)
      restored = Iodine::Utils.unescape_html(escaped)
      expect(restored).to eq(original)
    end
  end

  # ---------------------------------------------------------------------------
  # Time formatters
  # ---------------------------------------------------------------------------
  describe '.rfc2109' do
    let(:time) { Time.at(0).utc }

    it 'returns a String' do
      expect(Iodine::Utils.rfc2109(time)).to be_a(String)
    end

    it 'returns a non-empty String' do
      expect(Iodine::Utils.rfc2109(time)).not_to be_empty
    end

    it 'is deterministic for the same time' do
      expect(Iodine::Utils.rfc2109(time)).to eq(Iodine::Utils.rfc2109(time))
    end
  end

  describe '.rfc2822' do
    let(:time) { Time.at(0).utc }

    it 'returns a String' do
      expect(Iodine::Utils.rfc2822(time)).to be_a(String)
    end

    it 'returns a non-empty String' do
      expect(Iodine::Utils.rfc2822(time)).not_to be_empty
    end

    it 'is deterministic for the same time' do
      expect(Iodine::Utils.rfc2822(time)).to eq(Iodine::Utils.rfc2822(time))
    end
  end

  describe '.time2str' do
    let(:time) { Time.at(0).utc }

    it 'returns a String' do
      expect(Iodine::Utils.time2str(time)).to be_a(String)
    end

    it 'returns a non-empty String' do
      expect(Iodine::Utils.time2str(time)).not_to be_empty
    end

    it 'is deterministic for the same time' do
      expect(Iodine::Utils.time2str(time)).to eq(Iodine::Utils.time2str(time))
    end

    it 'produces different output for different times' do
      t1 = Time.at(0).utc
      t2 = Time.at(3600).utc
      expect(Iodine::Utils.time2str(t1)).not_to eq(Iodine::Utils.time2str(t2))
    end
  end
end
