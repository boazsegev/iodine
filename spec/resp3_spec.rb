# frozen_string_literal: true

require 'spec_helper'
require 'support/blocking_tcp_socket'

RESP3_URL = ENV.fetch('RESP3_URL', ENV.fetch('REDIS_URL', 'redis://localhost:6379/'))
RESP3_AVAILABLE = BlockingTcpSocket.resp3_available?(RESP3_URL)

# The preflight uses a short-lived blocking socket to verify that a local
# Valkey/Redis endpoint accepts HELLO 3. The C engine performs its own HELLO
# during construction, so this Ruby spec never constructs an engine or sends
# commands through one.
RSpec.describe Iodine::PubSub::Engine::RESP3,
               skip: !RESP3_AVAILABLE && 'RESP3 database not available' do
  it 'has a RESP3-capable endpoint available for engine integration tests' do
    expect(RESP3_AVAILABLE).to be true
  end

  it 'is the public replacement for the Redis engine' do
    expect(described_class).to be_a(Class)
    expect(defined?(Iodine::PubSub::Engine::Redis)).to be_nil
  end

  it 'exposes the connection-state API' do
    expect(described_class.instance_methods).to include(:connection_state)
  end
end
