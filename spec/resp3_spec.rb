# frozen_string_literal: true

require 'spec_helper'
require 'support/blocking_tcp_socket'
require 'support/iodine_test_batch'

RESP3_URL = ENV.fetch('RESP3_URL', ENV.fetch('REDIS_URL', 'redis://localhost:6379/'))
RESP3_AVAILABLE = BlockingTcpSocket.resp3_available?(RESP3_URL)

# Loud, format-independent report of the run/skip decision.
# The `:skip` metadata on the integration group below is filtered out entirely
# by `config.filter_run_excluding :skip` (see spec_helper), so without this
# line a skipped run would be indistinguishable from a performed one.
if RESP3_AVAILABLE
  warn "[RESP3 spec] endpoint #{RESP3_URL} accepts HELLO 3 - RESP3 engine specs will RUN"
else
  warn "[RESP3 spec] no RESP3 endpoint at #{RESP3_URL} (set RESP3_URL to enable) - RESP3 engine specs SKIPPED"
end

# Always-run reporter: a runtime `skip` is NOT removed by
# `filter_run_excluding :skip` (which only filters load-time metadata), so the
# summary always shows whether the integration group below ran or was skipped.
RSpec.describe 'RESP3 spec environment' do
  it 'reports whether the RESP3 engine specs ran' do
    skip "RESP3 engine specs SKIPPED - no RESP3 endpoint at #{RESP3_URL} (set RESP3_URL to enable)" unless RESP3_AVAILABLE

    expect(RESP3_AVAILABLE).to be true
  end
end

# The preflight uses a short-lived blocking socket to verify that a local
# Valkey/Redis endpoint accepts HELLO 3. The C engine performs its own HELLO
# during construction, so the preflight never sends a second HELLO through an
# engine instance.
RSpec.describe Iodine::PubSub::Engine::RESP3,
               skip: !RESP3_AVAILABLE && "RESP3 database not available at #{RESP3_URL}" do
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

  # Live integration: constructs a real engine, lets the reactor complete the
  # HELLO 3 handshake, and verifies both the state transition and command
  # execution against the endpoint found by the preflight.
  describe 'integration against a live RESP3 endpoint' do
    RESP3_BATCH = IodineTestBatch.new(threads: 1) # rubocop:disable RSpec/LeakyConstantDeclaration

    before(:context) do
      # Construct before Iodine.start so the engine attaches to the reactor;
      # the C constructor sends the sole HELLO 3 handshake by itself.
      engine = described_class.new(RESP3_URL, ping: 0)
      RESP3_BATCH.instance_variable_get(:@results)[:setup] =
        { engine: engine, initial_state: engine.connection_state }

      RESP3_BATCH.test(:lifecycle) do |r|
        r[:states] = []
        r[:ping] = nil
        r[:set] = nil
        r[:get] = nil
        key = "iodine:spec:resp3:#{Process.pid}"
        # Poll for readiness, bounded so the chain always ends before the
        # batch watchdog fires Iodine.stop (no stray run_after timers may
        # leak into later reactor cycles).
        attempts = 0
        poll = nil
        poll = lambda do
          attempts += 1
          state = engine.connection_state
          r[:states] << state
          if state == :connected
            engine.cmd('PING') { |res| r[:ping] = res }
            engine.cmd('SET', key, 'ok') { |res| r[:set] = res }
            engine.cmd('GET', key) { |res| r[:get] = res }
            engine.cmd('DEL', key)
          elsif state != :error && attempts < 10
            Iodine.run_after(20) { poll.call }
          end
        end
        poll.call
      end
      RESP3_BATCH.start!
    end

    it 'reports :connecting before the reactor completes the HELLO 3 handshake' do
      expect(RESP3_BATCH[:setup][:initial_state]).to eq(:connecting)
    end

    it 'reaches :connected once the reactor runs' do
      expect(RESP3_BATCH[:lifecycle][:states].last).to eq(:connected)
    end

    it 'executes PING over the live connection' do
      expect(RESP3_BATCH[:lifecycle][:ping]).to eq('PONG')
    end

    it 'executes a SET/GET round-trip over the live connection' do
      expect(RESP3_BATCH[:lifecycle][:set]).to eq('OK')
      expect(RESP3_BATCH[:lifecycle][:get]).to eq('ok')
    end
  end
end
