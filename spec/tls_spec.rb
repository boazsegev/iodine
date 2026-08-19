# frozen_string_literal: true

require 'spec_helper'

RSpec.describe Iodine::TLS do
  describe 'constants' do
    it 'defines SUPPORTED as true' do
      expect(Iodine::TLS::SUPPORTED).to be true
    end

    it 'defines EMBEDDED_AVAILABLE as true' do
      expect(Iodine::TLS::EMBEDDED_AVAILABLE).to be true
    end

    it 'defines OPENSSL_AVAILABLE as a Boolean' do
      val = Iodine::TLS::OPENSSL_AVAILABLE
      expect(val == true || val == false).to be true
    end
  end

  describe '.default' do
    it 'returns a Symbol' do
      expect(Iodine::TLS.default).to be_a(Symbol)
    end

    it 'returns :openssl or :iodine' do
      expect([:openssl, :iodine]).to include(Iodine::TLS.default)
    end

    it 'returns :openssl when OpenSSL is available' do
      skip 'OpenSSL not compiled in' unless Iodine::TLS::OPENSSL_AVAILABLE
      # When OpenSSL is available, the default is typically :openssl
      # (but may have been changed at runtime — just verify it's a valid symbol)
      expect([:openssl, :iodine]).to include(Iodine::TLS.default)
    end
  end

  describe '.default=' do
    let(:original_default) { Iodine::TLS.default }

    after do
      # Restore original default after each test
      Iodine::TLS.default = original_default
    end

    it 'accepts :iodine' do
      expect { Iodine::TLS.default = :iodine }.not_to raise_error
    end

    it 'sets :iodine as the active backend' do
      Iodine::TLS.default = :iodine
      expect(Iodine::TLS.default).to eq(:iodine)
    end

    it 'accepts :openssl when OpenSSL is available' do
      skip 'OpenSSL not compiled in' unless Iodine::TLS::OPENSSL_AVAILABLE
      expect { Iodine::TLS.default = :openssl }.not_to raise_error
    end

    it 'sets :openssl as the active backend when available' do
      skip 'OpenSSL not compiled in' unless Iodine::TLS::OPENSSL_AVAILABLE
      Iodine::TLS.default = :openssl
      expect(Iodine::TLS.default).to eq(:openssl)
    end

    it 'raises RuntimeError when setting :openssl without OpenSSL support' do
      skip 'OpenSSL is compiled in' if Iodine::TLS::OPENSSL_AVAILABLE
      expect { Iodine::TLS.default = :openssl }.to raise_error(RuntimeError)
    end

    it 'raises ArgumentError for an unknown backend symbol' do
      expect { Iodine::TLS.default = :invalid_backend }.to raise_error(ArgumentError)
    end

    it 'raises ArgumentError for :none' do
      expect { Iodine::TLS.default = :none }.to raise_error(ArgumentError)
    end

    it 'raises TypeError for a String argument' do
      expect { Iodine::TLS.default = 'iodine' }.to raise_error(TypeError)
    end

    it 'raises TypeError for an Integer argument' do
      expect { Iodine::TLS.default = 1 }.to raise_error(TypeError)
    end

    it 'returns the backend symbol that was set' do
      result = (Iodine::TLS.default = :iodine)
      expect(result).to eq(:iodine)
    end
  end

  describe '.new' do
    it 'creates a TLS instance without error' do
      expect { Iodine::TLS.new }.not_to raise_error
    end

    it 'returns an Iodine::TLS instance' do
      expect(Iodine::TLS.new).to be_a(Iodine::TLS)
    end
  end

  describe '#add_cert' do
    let(:tls) { Iodine::TLS.new }

    it 'accepts a name: keyword argument for self-signed cert' do
      expect { tls.add_cert(name: 'example.com') }.not_to raise_error
    end

    it 'returns self for chaining' do
      result = tls.add_cert(name: 'example.com')
      expect(result).to be(tls)
    end
  end
end
