# frozen_string_literal: true

require 'spec_helper'
require 'socket'
require 'weakref'

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
RAW_REFUSED_PORT = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }

RAW_RESULTS = {}

module RawTcpClientHandler
  def self.on_open(connection)
    RAW_RESULTS[:client_open] = true
    connection.write("PING\n")
  end

  def self.on_message(connection, data)
    RAW_RESULTS[:client_received] ||= +''
    RAW_RESULTS[:client_received] << data
    return unless RAW_RESULTS[:client_received].include?("HELLO\n")
    return unless RAW_RESULTS[:client_received].include?("PING\n")
    return if RAW_RESULTS[:client_closing]

    RAW_RESULTS[:greeting] = 'HELLO'
    RAW_RESULTS[:echo] = 'PING'
    RAW_RESULTS[:client_closing] = true
    connection.close
  end

  def self.on_close(_connection)
    RAW_RESULTS[:client_closed] = true
    RAW_RESULTS[:client] = nil
    Iodine.run_after(100) do
      RAW_RESULTS[:refused_terminal_callbacks] = 0
      handler = Object.new
      handler.define_singleton_method(:on_open) do |_connection|
        RAW_RESULTS[:refused_open] = true
      end
      handler.define_singleton_method(:on_close) do |_connection|
        RAW_RESULTS[:refused_closed] = true
        RAW_RESULTS[:refused_terminal_callbacks] += 1
        RAW_RESULTS[:finished] = true
        Iodine.run_after(100) { Iodine.stop }
      end
      RAW_RESULTS[:refused_handler] = WeakRef.new(handler)
      Iodine::Connection.new(
        "tcp://127.0.0.1:#{RAW_REFUSED_PORT}",
        handler: handler
      )
      handler = nil
    end
  end
end

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
    Iodine::Logger.debug "before(:context) start"

    Iodine.workers   = 0
    Iodine.threads   = 1
    Iodine::Logger.debug "config done, registering on_state"

    Iodine.on_state(:start) do
      Iodine::Logger.debug "on_state(:start) fired"
      next if RAW_STARTED[0]
      RAW_STARTED[0] = true
      Iodine.run_after(500) do
        RAW_RESULTS[:client] = Iodine::Connection.new(
          "tcp://127.0.0.1:#{RAW_PORT}",
          handler: RawTcpClientHandler
        )
      end
      # Timers survive reactor restarts, so an obsolete watchdog must not stop
      # a later spec's reactor cycle after this test completes normally.
      Iodine.run_after(5000) { Iodine.stop unless RAW_RESULTS[:finished] }
    end

    Iodine::Logger.debug "calling Iodine.start"
    Iodine.start
    Iodine::Logger.debug "Iodine.start returned"
  end

  it 'completes without errors' do
    expect(RAW_RESULTS[:error]).to be_nil, "Raw IO error: #{RAW_RESULTS[:error]}"
  end

  it 'fires on_open for both connections' do
    expect(RAW_RESULTS[:on_open]).to be true
    expect(RAW_RESULTS[:client_open]).to be true
  end

  it 'sends a greeting on connect' do
    expect(RAW_RESULTS[:greeting]).to eq('HELLO')
  end

  it 'echoes received data' do
    expect(RAW_RESULTS[:echo]).to eq('PING')
  end

  it 'fires on_close after disconnect' do
    expect(RAW_RESULTS[:on_close]).to be true
    expect(RAW_RESULTS[:client_closed]).to be true
  end

  it 'does not open a refused outbound connection' do
    expect(RAW_RESULTS[:refused_open]).to be_nil
    expect(RAW_RESULTS[:refused_closed]).to be true
    expect(RAW_RESULTS[:refused_terminal_callbacks]).to eq(1)
  end

  it 'releases the refused connection handler' do
    2.times { GC.start(full_mark: true, immediate_sweep: true) }
    expect(RAW_RESULTS[:refused_handler].weakref_alive?).to be_falsey
  end
end
