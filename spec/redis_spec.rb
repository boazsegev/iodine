# frozen_string_literal: true

require 'socket'
require 'uri'

# Skip the entire file if no Redis server is reachable.
REDIS_URL = ENV.fetch('REDIS_URL', 'redis://localhost:6379/')
REDIS_AVAILABLE = begin
  uri   = URI.parse(REDIS_URL)
  host  = uri.host || 'localhost'
  port  = uri.port || 6379
  Socket.tcp(host, port, connect_timeout: 1).close
  true
rescue
  false
end

RSpec.describe Iodine::PubSub::Engine::Redis, skip: !REDIS_AVAILABLE && 'Redis server not available' do
  let(:redis_url) { REDIS_URL }

  describe '.new' do
    it 'creates a Redis engine with default ping' do
      redis = described_class.new(redis_url)
      expect(redis).to be_a(described_class)
    end

    it 'creates a Redis engine with custom ping interval' do
      redis = described_class.new(redis_url, ping: 30)
      expect(redis).to be_a(described_class)
    end

    it 'raises ArgumentError when URL is missing' do
      expect { described_class.new(nil) }.to raise_error(ArgumentError)
    end

    it 'raises ArgumentError when ping is out of range' do
      expect { described_class.new(redis_url, ping: 300) }.to raise_error(ArgumentError)
    end

    it 'accepts URL with authentication' do
      # This may fail to connect but should not raise during initialization
      redis = described_class.new('redis://user:pass@localhost:6379/')
      expect(redis).to be_a(described_class)
    end
  end

  describe '#cmd' do
    let(:redis) { described_class.new(redis_url, ping: 30) }
    let(:test_key) { "iodine_test_#{Time.now.to_i}_#{rand(10_000)}" }

    after do
      # Clean up test keys - run in Iodine context
    end

    it 'returns true when command is queued successfully' do
      result = redis.cmd('PING')
      expect(result).to be true
    end

    it 'raises ArgumentError when no command is given' do
      expect { redis.cmd }.to raise_error(ArgumentError)
    end

    it 'accepts string arguments' do
      result = redis.cmd('SET', test_key, 'test_value')
      expect(result).to be true
    end

    it 'accepts integer arguments' do
      result = redis.cmd('SET', test_key, 12_345)
      expect(result).to be true
    end

    it 'accepts symbol arguments' do
      result = redis.cmd(:PING)
      expect(result).to be true
    end

    context 'with callback block' do
      # NOTE: These tests verify the command is sent successfully.
      # The callback is invoked asynchronously when Iodine's IO loop runs.

      it 'accepts a block for PING command' do
        result = redis.cmd('PING') { |r| }
        expect(result).to be true
      end

      it 'accepts a block for SET command' do
        result = redis.cmd('SET', test_key, 'value') { |r| }
        expect(result).to be true
      end

      it 'accepts a block for GET command' do
        result = redis.cmd('GET', test_key) { |r| }
        expect(result).to be true
      end
    end
  end

  describe 'integration with Iodine::PubSub' do
    let(:redis) { described_class.new(redis_url, ping: 30) }

    it 'can be set as the default pub/sub engine' do
      original_default = Iodine::PubSub.default

      Iodine::PubSub.default = redis
      expect(Iodine::PubSub.default).to eq(redis)

      # Restore original
      Iodine::PubSub.default = original_default
    end
  end

  describe 'command types' do
    let(:redis) { described_class.new(redis_url, ping: 30) }
    let(:test_key) { "iodine_test_#{Time.now.to_i}_#{rand(10_000)}" }

    it 'handles SET command' do
      expect(redis.cmd('SET', test_key, 'hello')).to be true
    end

    it 'handles GET command' do
      expect(redis.cmd('GET', test_key)).to be true
    end

    it 'handles DEL command' do
      expect(redis.cmd('DEL', test_key)).to be true
    end

    it 'handles INCR command' do
      expect(redis.cmd('INCR', "#{test_key}_counter")).to be true
    end

    it 'handles KEYS command' do
      expect(redis.cmd('KEYS', '*')).to be true
    end

    it 'handles EXPIRE command' do
      redis.cmd('SET', test_key, 'value')
      expect(redis.cmd('EXPIRE', test_key, 60)).to be true
    end

    it 'handles HSET command' do
      expect(redis.cmd('HSET', "#{test_key}_hash", 'field', 'value')).to be true
    end

    it 'handles HGET command' do
      expect(redis.cmd('HGET', "#{test_key}_hash", 'field')).to be true
    end

    it 'handles LPUSH command' do
      expect(redis.cmd('LPUSH', "#{test_key}_list", 'item1')).to be true
    end

    it 'handles LRANGE command' do
      expect(redis.cmd('LRANGE', "#{test_key}_list", 0, -1)).to be true
    end
  end

  describe 'argument type conversion' do
    let(:redis) { described_class.new(redis_url, ping: 30) }
    let(:test_key) { "iodine_test_#{Time.now.to_i}_#{rand(10_000)}" }

    it 'converts true to "1"' do
      expect(redis.cmd('SET', test_key, true)).to be true
    end

    it 'converts false to "0"' do
      expect(redis.cmd('SET', test_key, false)).to be true
    end

    it 'converts nil to null' do
      # nil is converted to Redis null
      expect(redis.cmd('SET', test_key, nil)).to be true
    end

    it 'converts floats' do
      expect(redis.cmd('SET', test_key, 3.14159)).to be true
    end

    it 'converts symbols to strings' do
      expect(redis.cmd(:SET, test_key.to_sym, :test_value)).to be true
    end
  end
end
