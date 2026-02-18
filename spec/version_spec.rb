# frozen_string_literal: true

require 'spec_helper'

RSpec.describe 'Iodine::VERSION' do
  it 'is a String' do
    expect(Iodine::VERSION).to be_a(String)
  end

  it 'matches semantic version format' do
    expect(Iodine::VERSION).to match(/\d+\.\d+\.\d+/)
  end

  it 'is not empty' do
    expect(Iodine::VERSION).not_to be_empty
  end

  it 'is frozen' do
    expect(Iodine::VERSION).to be_frozen
  end
end

RSpec.describe 'Iodine module' do
  it 'defines the Iodine module' do
    expect(defined?(Iodine)).to eq('constant')
  end

  it 'defines Iodine::Utils' do
    expect(defined?(Iodine::Utils)).to eq('constant')
  end

  it 'defines Iodine::TLS' do
    expect(defined?(Iodine::TLS)).to eq('constant')
  end

  it 'defines Iodine::JSON' do
    expect(defined?(Iodine::JSON)).to eq('constant')
  end

  it 'defines Iodine::Mustache' do
    expect(defined?(Iodine::Mustache)).to eq('constant')
  end

  it 'defines Iodine::Base::Crypto' do
    expect(defined?(Iodine::Base::Crypto)).to eq('constant')
  end

  describe '.extensions' do
    subject(:extensions) { Iodine.extensions }

    it 'returns a Hash' do
      expect(extensions).to be_a(Hash)
    end

    it 'includes neo_rack extension' do
      expect(extensions).to have_key(:neo_rack)
    end

    it 'includes rack extension' do
      expect(extensions).to have_key(:rack)
    end

    it 'includes ws extension' do
      expect(extensions).to have_key(:ws)
    end

    it 'includes pubsub extension' do
      expect(extensions).to have_key(:pubsub)
    end

    it 'has Array values (version tuples)' do
      extensions.each_value do |v|
        expect(v).to be_a(Array), "Expected Array for #{v.inspect}"
      end
    end
  end
end
