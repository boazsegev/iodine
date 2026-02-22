# frozen_string_literal: true

require 'spec_helper'
require 'socket'

# =============================================================================
# Iodine Raw IO (TCP) Integration Tests
#
# Isolates the reactor + raw connection callbacks from the HTTP pipeline.
# Uses plain TCP sockets so that any crash here implicates the core reactor
# and raw IO path rather than HTTP-specific code.
#
# Runs BEFORE http_spec.rb (alphabetical ordering, config.order = :defined).
#
# IMPORTANT: Iodine.listen survives until process exit. Call it once at
# file-load time, never inside on_state(:start) or before blocks.
# =============================================================================

RAW_PORT = (ENV['IODINE_TEST_PORT'] || 19_876).to_i + 10  # avoid clash with http_spec

RAW_RESULTS = {}

# ---------------------------------------------------------------------------
# Raw IO handler — echoes received data, records lifecycle events
# ---------------------------------------------------------------------------
module RawEchoHandler
  def self.on_open(c)
    RAW_RESULTS[:on_open] = true
    c.write("HELLO\n")
  end

  def self.on_message(c, data)
    RAW_RESULTS[:received] ||= +''
    RAW_RESULTS[:received] << data
    c.write(data)   # echo back
  end

  def self.on_close(c)
    RAW_RESULTS[:on_close] = true
  end
end

Iodine.listen(url: "tcp://127.0.0.1:#{RAW_PORT}", handler: RawEchoHandler)

# ---------------------------------------------------------------------------
RSpec.describe 'Iodine raw TCP connection' do
  RAW_STARTED = [false]  # rubocop:disable RSpec/LeakyConstantDeclaration

  before(:context) do
    Iodine.verbosity = 0
    Iodine.workers   = 0
    Iodine.threads   = 1   # raw IO needs only one worker thread

    run_tests = proc do
      TCPSocket.open('127.0.0.1', RAW_PORT) do |sock|
        # Read the server greeting
        greeting = sock.gets
        RAW_RESULTS[:greeting] = greeting&.chomp

        # Send a line and read the echo
        sock.write("PING\n")
        sock.flush
        echo = sock.gets
        RAW_RESULTS[:echo] = echo&.chomp
      end
    rescue => e
      RAW_RESULTS[:error] = "#{e.class}: #{e.message}"
    ensure
      Iodine.run_after(100) { Iodine.stop }
    end

    Iodine.on_state(:start) do
      next if RAW_STARTED[0]
      RAW_STARTED[0] = true
      Iodine.run_after(50)   { Iodine.async { run_tests.call } }
      Iodine.run_after(5000) { Iodine.stop }
    end

    Iodine.start
  end

  it 'completes without errors' do
    expect(RAW_RESULTS[:error]).to be_nil, "Raw IO error: #{RAW_RESULTS[:error]}"
  end

  it 'fires on_open' do
    expect(RAW_RESULTS[:on_open]).to be true
  end

  it 'sends a greeting on connect' do
    expect(RAW_RESULTS[:greeting]).to eq('HELLO')
  end

  it 'echoes received data' do
    expect(RAW_RESULTS[:echo]).to eq('PING')
  end

  it 'fires on_close after disconnect' do
    expect(RAW_RESULTS[:on_close]).to be true
  end
end
