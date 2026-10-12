# frozen_string_literal: true

require 'spec_helper'
require 'zlib'
require 'stringio'

RSpec.describe Iodine::Base::Compression do
  [described_class::Deflate, described_class::Gzip, described_class::Brotli].each do |codec|
    describe codec.name do
      it 'round trips empty and compressible data with a server-friendly default' do
        ['', 'hello server ' * 2000].each do |data|
          packed = codec.compress(data)
          expect(codec.decompress(packed)).to eq(data)
          expect(packed).to eq(codec.compress(data, codec == described_class::Brotli ? { quality: 3 } : { level: 3 }))
        end
      end

      it 'rejects invalid streams and limits decompressed output' do
        expect { codec.decompress('invalid') }.to raise_error(RuntimeError)
        expect { codec.decompress('') }.to raise_error(RuntimeError)
        packed = codec.compress('a' * 4096)
        expect { codec.decompress(packed, max_output: 4095) }.to raise_error(ArgumentError, /max_output/)
        expect(codec.decompress(packed, max_output: 4096)).to eq('a' * 4096)
      end
    end
  end

  it 'produces standard gzip and deflate streams' do
    input = 'interop' * 100
    expect(Zlib::GzipReader.new(StringIO.new(described_class::Gzip.compress(input))).read).to eq(input)
    inflater = Zlib::Inflate.new(-Zlib::MAX_WBITS)
    expect(inflater.inflate(described_class::Deflate.compress(input))).to eq(input)
    inflater.close
  end

  it 'rejects a gzip stream with a bad CRC after size probing' do
    compressed = described_class::Gzip.compress('payload' * 100)
    compressed.setbyte(-8, compressed.getbyte(-8) ^ 1)
    expect { described_class::Gzip.decompress(compressed) }.to raise_error(RuntimeError)
  end

  it 'accepts Brotli quality 6 and rejects quality 7' do
    codec = described_class::Brotli
    expect(codec.decompress(codec.compress('test' * 100, quality: 6))).to eq('test' * 100)
    expect { codec.compress('test', quality: 7) }.to raise_error(ArgumentError)
  end
end
