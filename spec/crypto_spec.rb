# frozen_string_literal: true

require 'spec_helper'
require 'securerandom'

RSpec.describe Iodine::Base::Crypto do
  # ---------------------------------------------------------------------------
  # ChaCha20-Poly1305
  # ---------------------------------------------------------------------------
  describe 'ChaCha20Poly1305' do
    let(:mod)   { Iodine::Base::Crypto::ChaCha20Poly1305 }
    let(:key)   { SecureRandom.random_bytes(32) }
    let(:nonce) { SecureRandom.random_bytes(12) }
    let(:plaintext) { 'Hello, World! This is a test message.' }

    describe '.encrypt' do
      it 'returns an Array of two Strings' do
        result = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
        expect(result[0]).to be_a(String)
        expect(result[1]).to be_a(String)
      end

      it 'returns ciphertext of the same length as plaintext' do
        ciphertext, _mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(ciphertext.bytesize).to eq(plaintext.bytesize)
      end

      it 'returns a 16-byte MAC' do
        _ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(mac.bytesize).to eq(16)
      end

      it 'raises ArgumentError for wrong key size' do
        expect { mod.encrypt(plaintext, key: 'short', nonce: nonce) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong nonce size' do
        expect { mod.encrypt(plaintext, key: key, nonce: 'short') }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for 24-byte nonce (XChaCha nonce)' do
        expect { mod.encrypt(plaintext, key: key, nonce: SecureRandom.random_bytes(24)) }.to raise_error(ArgumentError)
      end
    end

    describe '.decrypt' do
      it 'decrypts to the original plaintext' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        result = mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
        expect(result).to eq(plaintext)
      end

      it 'raises RuntimeError when MAC is tampered' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        bad_mac = mac.dup
        bad_mac.setbyte(0, bad_mac.getbyte(0) ^ 0xFF)
        expect { mod.decrypt(ciphertext, mac: bad_mac, key: key, nonce: nonce) }.to raise_error(RuntimeError)
      end

      it 'raises RuntimeError when ciphertext is tampered' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        bad_ct = ciphertext.dup
        bad_ct.setbyte(0, bad_ct.getbyte(0) ^ 0xFF)
        expect { mod.decrypt(bad_ct, mac: mac, key: key, nonce: nonce) }.to raise_error(RuntimeError)
      end

      it 'raises RuntimeError when wrong key is used' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        wrong_key = SecureRandom.random_bytes(32)
        expect { mod.decrypt(ciphertext, mac: mac, key: wrong_key, nonce: nonce) }.to raise_error(RuntimeError)
      end

      it 'raises ArgumentError for wrong key size' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect { mod.decrypt(ciphertext, mac: mac, key: 'short', nonce: nonce) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong nonce size' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect { mod.decrypt(ciphertext, mac: mac, key: key, nonce: 'short') }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong MAC size' do
        ciphertext, _mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect { mod.decrypt(ciphertext, mac: 'short', key: key, nonce: nonce) }.to raise_error(ArgumentError)
      end
    end

    describe 'round-trip with additional data' do
      let(:ad) { 'additional authenticated data' }

      it 'decrypts correctly with matching AD' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce, ad: ad)
        result = mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce, ad: ad)
        expect(result).to eq(plaintext)
      end

      it 'raises RuntimeError when AD is different on decrypt' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce, ad: ad)
        expect { mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce, ad: 'wrong ad') }.to raise_error(RuntimeError)
      end
    end

    it 'encrypts empty plaintext successfully' do
      ciphertext, mac = mod.encrypt('', key: key, nonce: nonce)
      result = mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
      expect(result).to eq('')
    end
  end

  # ---------------------------------------------------------------------------
  # XChaCha20-Poly1305
  # ---------------------------------------------------------------------------
  describe 'XChaCha20Poly1305' do
    let(:mod)   { Iodine::Base::Crypto::XChaCha20Poly1305 }
    let(:key)   { SecureRandom.random_bytes(32) }
    let(:nonce) { SecureRandom.random_bytes(24) }
    let(:plaintext) { 'XChaCha20 test message with extended nonce.' }

    describe '.encrypt' do
      it 'returns an Array of two Strings' do
        result = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
      end

      it 'returns ciphertext of the same length as plaintext' do
        ciphertext, _mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(ciphertext.bytesize).to eq(plaintext.bytesize)
      end

      it 'returns a 16-byte MAC' do
        _ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(mac.bytesize).to eq(16)
      end

      it 'raises ArgumentError for 12-byte nonce (ChaCha nonce)' do
        expect { mod.encrypt(plaintext, key: key, nonce: SecureRandom.random_bytes(12)) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong key size' do
        expect { mod.encrypt(plaintext, key: 'short', nonce: nonce) }.to raise_error(ArgumentError)
      end
    end

    describe '.decrypt' do
      it 'decrypts to the original plaintext' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        result = mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
        expect(result).to eq(plaintext)
      end

      it 'raises RuntimeError when MAC is tampered' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        bad_mac = mac.dup
        bad_mac.setbyte(0, bad_mac.getbyte(0) ^ 0xFF)
        expect { mod.decrypt(ciphertext, mac: bad_mac, key: key, nonce: nonce) }.to raise_error(RuntimeError)
      end

      it 'raises RuntimeError when wrong key is used' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        wrong_key = SecureRandom.random_bytes(32)
        expect { mod.decrypt(ciphertext, mac: mac, key: wrong_key, nonce: nonce) }.to raise_error(RuntimeError)
      end
    end

    it 'is safe to use with randomly generated nonces' do
      # XChaCha20 24-byte nonce is safe for random generation
      nonce1 = SecureRandom.random_bytes(24)
      nonce2 = SecureRandom.random_bytes(24)
      ct1, mac1 = mod.encrypt(plaintext, key: key, nonce: nonce1)
      ct2, mac2 = mod.encrypt(plaintext, key: key, nonce: nonce2)
      # Different nonces produce different ciphertexts
      expect(ct1).not_to eq(ct2)
      # Both decrypt correctly
      expect(mod.decrypt(ct1, mac: mac1, key: key, nonce: nonce1)).to eq(plaintext)
      expect(mod.decrypt(ct2, mac: mac2, key: key, nonce: nonce2)).to eq(plaintext)
    end
  end

  # ---------------------------------------------------------------------------
  # AES-128-GCM
  # ---------------------------------------------------------------------------
  describe 'AES128GCM' do
    let(:mod)   { Iodine::Base::Crypto::AES128GCM }
    let(:key)   { SecureRandom.random_bytes(16) }
    let(:nonce) { SecureRandom.random_bytes(12) }
    let(:plaintext) { 'AES-128-GCM test message.' }

    describe '.encrypt' do
      it 'returns an Array of two Strings' do
        result = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
      end

      it 'returns ciphertext of the same length as plaintext' do
        ciphertext, _mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(ciphertext.bytesize).to eq(plaintext.bytesize)
      end

      it 'returns a 16-byte MAC' do
        _ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(mac.bytesize).to eq(16)
      end

      it 'raises ArgumentError for wrong key size (32 bytes instead of 16)' do
        expect { mod.encrypt(plaintext, key: SecureRandom.random_bytes(32), nonce: nonce) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong nonce size' do
        expect { mod.encrypt(plaintext, key: key, nonce: 'short') }.to raise_error(ArgumentError)
      end
    end

    describe '.decrypt' do
      it 'decrypts to the original plaintext' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        result = mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
        expect(result).to eq(plaintext)
      end

      it 'raises RuntimeError when MAC is tampered' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        bad_mac = mac.dup
        bad_mac.setbyte(0, bad_mac.getbyte(0) ^ 0xFF)
        expect { mod.decrypt(ciphertext, mac: bad_mac, key: key, nonce: nonce) }.to raise_error(RuntimeError)
      end

      it 'raises RuntimeError when wrong key is used' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        wrong_key = SecureRandom.random_bytes(16)
        expect { mod.decrypt(ciphertext, mac: mac, key: wrong_key, nonce: nonce) }.to raise_error(RuntimeError)
      end
    end

    it 'encrypts empty plaintext successfully' do
      ciphertext, mac = mod.encrypt('', key: key, nonce: nonce)
      result = mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
      expect(result).to eq('')
    end
  end

  # ---------------------------------------------------------------------------
  # AES-256-GCM
  # ---------------------------------------------------------------------------
  describe 'AES256GCM' do
    let(:mod)   { Iodine::Base::Crypto::AES256GCM }
    let(:key)   { SecureRandom.random_bytes(32) }
    let(:nonce) { SecureRandom.random_bytes(12) }
    let(:plaintext) { 'AES-256-GCM test message.' }

    describe '.encrypt' do
      it 'returns an Array of two Strings' do
        result = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
      end

      it 'returns ciphertext of the same length as plaintext' do
        ciphertext, _mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(ciphertext.bytesize).to eq(plaintext.bytesize)
      end

      it 'returns a 16-byte MAC' do
        _ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        expect(mac.bytesize).to eq(16)
      end

      it 'raises ArgumentError for wrong key size (16 bytes instead of 32)' do
        expect { mod.encrypt(plaintext, key: SecureRandom.random_bytes(16), nonce: nonce) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong nonce size' do
        expect { mod.encrypt(plaintext, key: key, nonce: 'short') }.to raise_error(ArgumentError)
      end
    end

    describe '.decrypt' do
      it 'decrypts to the original plaintext' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        result = mod.decrypt(ciphertext, mac: mac, key: key, nonce: nonce)
        expect(result).to eq(plaintext)
      end

      it 'raises RuntimeError when MAC is tampered' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        bad_mac = mac.dup
        bad_mac.setbyte(0, bad_mac.getbyte(0) ^ 0xFF)
        expect { mod.decrypt(ciphertext, mac: bad_mac, key: key, nonce: nonce) }.to raise_error(RuntimeError)
      end

      it 'raises RuntimeError when wrong key is used' do
        ciphertext, mac = mod.encrypt(plaintext, key: key, nonce: nonce)
        wrong_key = SecureRandom.random_bytes(32)
        expect { mod.decrypt(ciphertext, mac: mac, key: wrong_key, nonce: nonce) }.to raise_error(RuntimeError)
      end
    end
  end

  # ---------------------------------------------------------------------------
  # Ed25519
  # ---------------------------------------------------------------------------
  describe 'Ed25519' do
    let(:mod) { Iodine::Base::Crypto::Ed25519 }
    let(:keypair) { mod.keypair }
    let(:sk) { keypair[0] }
    let(:pk) { keypair[1] }
    let(:message) { 'Sign this message.' }

    describe '.keypair' do
      it 'returns an Array of two Strings' do
        result = mod.keypair
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
        expect(result[0]).to be_a(String)
        expect(result[1]).to be_a(String)
      end

      it 'returns a 32-byte secret key' do
        expect(sk.bytesize).to eq(32)
      end

      it 'returns a 32-byte public key' do
        expect(pk.bytesize).to eq(32)
      end

      it 'generates unique keypairs on successive calls' do
        sk1, pk1 = mod.keypair
        sk2, pk2 = mod.keypair
        expect(sk1).not_to eq(sk2)
        expect(pk1).not_to eq(pk2)
      end
    end

    describe '.sign' do
      it 'returns a 64-byte signature' do
        sig = mod.sign(message, secret_key: sk, public_key: pk)
        expect(sig).to be_a(String)
        expect(sig.bytesize).to eq(64)
      end

      it 'is deterministic for the same inputs' do
        sig1 = mod.sign(message, secret_key: sk, public_key: pk)
        sig2 = mod.sign(message, secret_key: sk, public_key: pk)
        expect(sig1).to eq(sig2)
      end

      it 'raises ArgumentError for wrong secret key size' do
        expect { mod.sign(message, secret_key: 'short', public_key: pk) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong public key size' do
        expect { mod.sign(message, secret_key: sk, public_key: 'short') }.to raise_error(ArgumentError)
      end
    end

    describe '.verify' do
      it 'returns true for a valid signature' do
        sig = mod.sign(message, secret_key: sk, public_key: pk)
        expect(mod.verify(sig, message, public_key: pk)).to be true
      end

      it 'returns false for a tampered message' do
        sig = mod.sign(message, secret_key: sk, public_key: pk)
        expect(mod.verify(sig, 'tampered message', public_key: pk)).to be false
      end

      it 'returns false for a tampered signature' do
        sig = mod.sign(message, secret_key: sk, public_key: pk)
        bad_sig = sig.dup
        bad_sig.setbyte(0, bad_sig.getbyte(0) ^ 0xFF)
        expect(mod.verify(bad_sig, message, public_key: pk)).to be false
      end

      it 'returns false when verified with a different public key' do
        sig = mod.sign(message, secret_key: sk, public_key: pk)
        _sk2, pk2 = mod.keypair
        expect(mod.verify(sig, message, public_key: pk2)).to be false
      end

      it 'raises ArgumentError for wrong signature size' do
        expect { mod.verify('short', message, public_key: pk) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong public key size' do
        sig = mod.sign(message, secret_key: sk, public_key: pk)
        expect { mod.verify(sig, message, public_key: 'short') }.to raise_error(ArgumentError)
      end
    end

    describe '.public_key' do
      it 'derives the public key from the secret key' do
        derived_pk = mod.public_key(secret_key: sk)
        expect(derived_pk).to eq(pk)
      end

      it 'returns a 32-byte String' do
        expect(mod.public_key(secret_key: sk).bytesize).to eq(32)
      end

      it 'raises ArgumentError for wrong secret key size' do
        expect { mod.public_key(secret_key: 'short') }.to raise_error(ArgumentError)
      end
    end
  end

  # ---------------------------------------------------------------------------
  # X25519
  # ---------------------------------------------------------------------------
  describe 'X25519' do
    let(:mod) { Iodine::Base::Crypto::X25519 }
    let(:alice_keypair) { mod.keypair }
    let(:alice_sk) { alice_keypair[0] }
    let(:alice_pk) { alice_keypair[1] }
    let(:bob_keypair) { mod.keypair }
    let(:bob_sk) { bob_keypair[0] }
    let(:bob_pk) { bob_keypair[1] }
    let(:message) { 'Secret message for X25519 ECIES.' }

    describe '.keypair' do
      it 'returns an Array of two 32-byte Strings' do
        result = mod.keypair
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
        expect(result[0].bytesize).to eq(32)
        expect(result[1].bytesize).to eq(32)
      end

      it 'generates unique keypairs' do
        sk1, pk1 = mod.keypair
        sk2, pk2 = mod.keypair
        expect(sk1).not_to eq(sk2)
        expect(pk1).not_to eq(pk2)
      end
    end

    describe '.public_key' do
      it 'derives the public key from the secret key' do
        derived_pk = mod.public_key(secret_key: alice_sk)
        expect(derived_pk).to eq(alice_pk)
      end

      it 'returns a 32-byte String' do
        expect(mod.public_key(secret_key: alice_sk).bytesize).to eq(32)
      end
    end

    describe '.shared_secret (ECDH agreement)' do
      it 'computes the same shared secret from both sides' do
        alice_shared = mod.shared_secret(secret_key: alice_sk, their_public: bob_pk)
        bob_shared   = mod.shared_secret(secret_key: bob_sk,   their_public: alice_pk)
        expect(alice_shared).to eq(bob_shared)
      end

      it 'returns a 32-byte String' do
        shared = mod.shared_secret(secret_key: alice_sk, their_public: bob_pk)
        expect(shared.bytesize).to eq(32)
      end

      it 'raises ArgumentError for wrong key sizes' do
        expect { mod.shared_secret(secret_key: 'short', their_public: bob_pk) }.to raise_error(ArgumentError)
        expect { mod.shared_secret(secret_key: alice_sk, their_public: 'short') }.to raise_error(ArgumentError)
      end
    end

    describe '.encrypt / .decrypt (ChaCha20 ECIES)' do
      it 'decrypts to the original message' do
        ciphertext = mod.encrypt(message, recipient_pk: alice_pk)
        result = mod.decrypt(ciphertext, secret_key: alice_sk)
        expect(result).to eq(message)
      end

      it 'ciphertext is message.length + 48 bytes' do
        ciphertext = mod.encrypt(message, recipient_pk: alice_pk)
        expect(ciphertext.bytesize).to eq(message.bytesize + 48)
      end

      it 'raises RuntimeError when decrypted with wrong key' do
        ciphertext = mod.encrypt(message, recipient_pk: alice_pk)
        expect { mod.decrypt(ciphertext, secret_key: bob_sk) }.to raise_error(RuntimeError)
      end

      it 'raises ArgumentError for wrong recipient_pk size' do
        expect { mod.encrypt(message, recipient_pk: 'short') }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for ciphertext too short' do
        expect { mod.decrypt('short', secret_key: alice_sk) }.to raise_error(ArgumentError)
      end
    end

    describe '.encrypt_aes128 / .decrypt_aes128' do
      it 'decrypts to the original message' do
        ciphertext = mod.encrypt_aes128(message, recipient_pk: alice_pk)
        result = mod.decrypt_aes128(ciphertext, secret_key: alice_sk)
        expect(result).to eq(message)
      end

      it 'ciphertext is message.length + 48 bytes' do
        ciphertext = mod.encrypt_aes128(message, recipient_pk: alice_pk)
        expect(ciphertext.bytesize).to eq(message.bytesize + 48)
      end

      it 'raises RuntimeError when decrypted with wrong key' do
        ciphertext = mod.encrypt_aes128(message, recipient_pk: alice_pk)
        expect { mod.decrypt_aes128(ciphertext, secret_key: bob_sk) }.to raise_error(RuntimeError)
      end
    end

    describe '.encrypt_aes256 / .decrypt_aes256' do
      it 'decrypts to the original message' do
        ciphertext = mod.encrypt_aes256(message, recipient_pk: alice_pk)
        result = mod.decrypt_aes256(ciphertext, secret_key: alice_sk)
        expect(result).to eq(message)
      end

      it 'ciphertext is message.length + 48 bytes' do
        ciphertext = mod.encrypt_aes256(message, recipient_pk: alice_pk)
        expect(ciphertext.bytesize).to eq(message.bytesize + 48)
      end

      it 'raises RuntimeError when decrypted with wrong key' do
        ciphertext = mod.encrypt_aes256(message, recipient_pk: alice_pk)
        expect { mod.decrypt_aes256(ciphertext, secret_key: bob_sk) }.to raise_error(RuntimeError)
      end
    end
  end

  # ---------------------------------------------------------------------------
  # HKDF
  # ---------------------------------------------------------------------------
  describe 'HKDF' do
    let(:mod) { Iodine::Base::Crypto::HKDF }
    let(:ikm)  { 'input keying material' }
    let(:salt) { 'random salt value' }
    let(:info) { 'application context info' }

    describe '.derive' do
      it 'returns a String' do
        expect(mod.derive(ikm: ikm)).to be_a(String)
      end

      it 'returns 32 bytes by default' do
        expect(mod.derive(ikm: ikm).bytesize).to eq(32)
      end

      it 'respects the length: parameter' do
        expect(mod.derive(ikm: ikm, length: 16).bytesize).to eq(16)
        expect(mod.derive(ikm: ikm, length: 64).bytesize).to eq(64)
      end

      it 'is deterministic for the same inputs' do
        r1 = mod.derive(ikm: ikm, salt: salt, info: info, length: 32)
        r2 = mod.derive(ikm: ikm, salt: salt, info: info, length: 32)
        expect(r1).to eq(r2)
      end

      it 'produces different output with different salt' do
        r1 = mod.derive(ikm: ikm, salt: 'salt1', length: 32)
        r2 = mod.derive(ikm: ikm, salt: 'salt2', length: 32)
        expect(r1).not_to eq(r2)
      end

      it 'produces different output with different IKM' do
        r1 = mod.derive(ikm: 'ikm1', salt: salt, length: 32)
        r2 = mod.derive(ikm: 'ikm2', salt: salt, length: 32)
        expect(r1).not_to eq(r2)
      end

      it 'produces different output with different info' do
        r1 = mod.derive(ikm: ikm, salt: salt, info: 'info1', length: 32)
        r2 = mod.derive(ikm: ikm, salt: salt, info: 'info2', length: 32)
        expect(r1).not_to eq(r2)
      end

      it 'raises ArgumentError for length: 0' do
        expect { mod.derive(ikm: ikm, length: 0) }.to raise_error(ArgumentError)
      end

      it 'accepts sha384: true for SHA-384 based HKDF' do
        result = mod.derive(ikm: ikm, length: 48, sha384: true)
        expect(result.bytesize).to eq(48)
      end

      it 'produces different output with sha384: true vs false' do
        r1 = mod.derive(ikm: ikm, salt: salt, length: 32, sha384: false)
        r2 = mod.derive(ikm: ikm, salt: salt, length: 32, sha384: true)
        expect(r1).not_to eq(r2)
      end
    end
  end

  # ---------------------------------------------------------------------------
  # X25519MLKEM768 (Post-Quantum Hybrid KEM)
  # ---------------------------------------------------------------------------
  describe 'X25519MLKEM768' do
    let(:mod) { Iodine::Base::Crypto::X25519MLKEM768 }

    describe '.keypair' do
      it 'returns an Array of two Strings' do
        result = mod.keypair
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
      end

      it 'returns a 2432-byte secret key' do
        sk, _pk = mod.keypair
        expect(sk.bytesize).to eq(2432)
      end

      it 'returns a 1216-byte public key' do
        _sk, pk = mod.keypair
        expect(pk.bytesize).to eq(1216)
      end
    end

    describe '.encapsulate / .decapsulate' do
      let(:keypair) { mod.keypair }
      let(:sk) { keypair[0] }
      let(:pk) { keypair[1] }

      it 'encapsulate returns [ciphertext, shared_secret]' do
        result = mod.encapsulate(public_key: pk)
        expect(result).to be_a(Array)
        expect(result.length).to eq(2)
      end

      it 'encapsulate returns 1120-byte ciphertext' do
        ct, _ss = mod.encapsulate(public_key: pk)
        expect(ct.bytesize).to eq(1120)
      end

      it 'encapsulate returns 64-byte shared secret' do
        _ct, ss = mod.encapsulate(public_key: pk)
        expect(ss.bytesize).to eq(64)
      end

      it 'decapsulate recovers the same shared secret' do
        ct, ss_enc = mod.encapsulate(public_key: pk)
        ss_dec = mod.decapsulate(ciphertext: ct, secret_key: sk)
        expect(ss_dec).to eq(ss_enc)
      end

      it 'decapsulate returns a 64-byte shared secret' do
        ct, _ss = mod.encapsulate(public_key: pk)
        ss = mod.decapsulate(ciphertext: ct, secret_key: sk)
        expect(ss.bytesize).to eq(64)
      end

      it 'raises ArgumentError for wrong public key size' do
        expect { mod.encapsulate(public_key: 'short') }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong ciphertext size' do
        expect { mod.decapsulate(ciphertext: 'short', secret_key: sk) }.to raise_error(ArgumentError)
      end

      it 'raises ArgumentError for wrong secret key size' do
        ct, _ss = mod.encapsulate(public_key: pk)
        expect { mod.decapsulate(ciphertext: ct, secret_key: 'short') }.to raise_error(ArgumentError)
      end
    end
  end
end
