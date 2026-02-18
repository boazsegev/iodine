# frozen_string_literal: true

require 'spec_helper'

RSpec.describe 'Iodine::Benchmark' do
  it 'loads without errors' do
    expect { require 'iodine/benchmark' }.not_to raise_error
  end

  it 'defines all expected benchmark methods' do
    require 'iodine/benchmark'
    expected_methods = %i[
      json mustache random utils minimap
      crypto_symmetric crypto_asymmetric hashing
      crypto_ecies crypto_kdf crypto_totp crypto_all
      compression non_crypto_hashing
    ]
    expected_methods.each do |m|
      expect(Iodine::Benchmark).to respond_to(m),
        "Expected Iodine::Benchmark to respond to .#{m}"
    end
  end
end
