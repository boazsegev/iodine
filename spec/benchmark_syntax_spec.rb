# frozen_string_literal: true

require 'spec_helper'

RSpec.describe 'Iodine::Benchmark' do
  it 'loads without errors' do
    expect { require 'iodine/benchmark' }.not_to raise_error
  end

  it 'defines all expected benchmark methods' do
    require 'iodine/benchmark'
    expected_methods = %i[
      json mustache random hmac utils minimap
      crypto_symmetric crypto_asymmetric hashing
      crypto_ecies crypto_kdf crypto_totp crypto_all
      compression non_crypto_hashing
    ]
    expected_methods.each do |m|
      expect(Iodine::Benchmark).to respond_to(m),
        "Expected Iodine::Benchmark to respond to .#{m}"
    end
  end

  describe 'random and HMAC benchmark groups' do
    let(:labels) { [] }

    before do
      require 'iodine/benchmark'
      stub_const('Benchmark', Module.new)
      allow(Iodine::Benchmark).to receive(:require).and_call_original
      allow(Iodine::Benchmark).to receive(:require).with('benchmark/ips').and_return(true)
      reporter = double('benchmark reporter')
      allow(reporter).to receive(:report) { |label, &_block| labels << label }
      allow(reporter).to receive(:compare!)
      ::Benchmark.define_singleton_method(:ips) { |&block| block.call(reporter) }
      allow(Iodine::Benchmark).to receive(:puts)
    end

    it 'benchmarks secure random alongside other random generators, without MACs' do
      Iodine::Benchmark.random
      expect(labels).to include('Iodine::Utils.secure_random(bytes: 16)',
                                'Iodine::Utils.secure_random(bytes: 4096)')
      expect(labels).not_to include(a_string_matching(/hmac|HMAC/i))
    end

    it 'benchmarks MACs independently of random generation' do
      Iodine::Benchmark.hmac
      expect(labels).to include(a_string_matching(/Iodine::Utils.hmac256/))
      expect(labels).not_to include(a_string_matching(/secure_random|random_bytes/))
    end
  end
end
