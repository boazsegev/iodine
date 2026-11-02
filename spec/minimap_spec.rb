# frozen_string_literal: true

require 'spec_helper'

RSpec.describe Iodine::Base::MiniMap do
  it 'accepts true, false and nil keys' do
    m = described_class.new
    m[true] = 1
    m[false] = 2
    m[nil] = 3
    expect([m[true], m[false], m[nil]]).to eq([1, 2, 3])
    expect(m.count).to eq(3)
  end

  it 'accepts String, Symbol and Integer keys' do
    m = described_class.new
    m['a'] = 1
    m[:b] = 2
    m[3] = 3
    expect([m['a'], m[:b], m[3]]).to eq([1, 2, 3])
  end

  it 'rejects other key types' do
    expect { described_class.new[1.5] = 1 }.to raise_error(TypeError)
  end
end
