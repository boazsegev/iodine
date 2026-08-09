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
RAW_REFUSED_CLIENT_COUNT = 8

RAW_RESULTS = {}

# Records the Iodine STORE (GC-protection map) size before any client is
# created, so the specs can assert that every client-side hold was released.
# Unlike WeakRef + GC.start, this is deterministic across Ruby versions:
# conservative stack scanning pins dead objects on some Rubies (3.2, Windows),
# making GC-based liveness assertions flaky even when the C lifecycle is
# correct.
RAW_STORE_BASELINE = [nil]

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
      RAW_RESULTS[:refused_store_baseline] = Iodine::Base.store_size
      RAW_RESULTS[:refused_open] = []
      RAW_RESULTS[:refused_terminal_callbacks] =
        Array.new(RAW_REFUSED_CLIENT_COUNT, 0)
      RAW_RESULTS[:refused_handlers] = []

      RAW_REFUSED_CLIENT_COUNT.times do |index|
        handler = Object.new
        handler.define_singleton_method(:on_open) do |_connection|
          RAW_RESULTS[:refused_open] << index
        end
        handler.define_singleton_method(:on_close) do |_connection|
          RAW_RESULTS[:refused_terminal_callbacks][index] += 1
          next unless RAW_RESULTS[:refused_terminal_callbacks].sum ==
                      RAW_REFUSED_CLIENT_COUNT

          RAW_RESULTS[:finished] = true
          Iodine.run_after(100) do
            RAW_RESULTS[:refused_store_after] = Iodine::Base.store_size
            Iodine.stop
          end
        end
        RAW_RESULTS[:refused_handlers] << WeakRef.new(handler)
        Iodine::Connection.new(
          "tcp://127.0.0.1:#{RAW_REFUSED_PORT}",
          handler: handler
        )
      end
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
      RAW_STORE_BASELINE[0] = Iodine::Base.store_size
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

  it 'closes every refused outbound connection exactly once without opening' do
    expect(RAW_RESULTS[:refused_open]).to be_empty
    expect(RAW_RESULTS[:refused_terminal_callbacks]).to all(eq(1))
  end

  it 'releases every refused-client GC hold' do
    expect(RAW_RESULTS[:refused_store_after]).to eq(
      RAW_RESULTS[:refused_store_baseline]
    )
  end

  it 'does not increase the GC store across the reactor cycle' do
    # Startup holds can legitimately be released during stop, so this broader
    # cycle-level check may fall below its baseline.
    expect(Iodine::Base.store_size).to be <= RAW_STORE_BASELINE[0]
  end

  it 'collects the refused connection handlers once unpinned' do
    skip 'refused connections never ran' unless RAW_RESULTS[:refused_handlers]
    # GC liveness is best-effort: CRuby pins stale stack slots conservatively
    # (observed on 3.2 and Windows builds), so this only proves the objects are
    # collectable, it can never prove a leak. The store-balance test above is
    # the authoritative leak check.
    2.times { GC.start(full_mark: true, immediate_sweep: true) }
    if RAW_RESULTS[:refused_handlers].any?(&:weakref_alive?)
      skip 'handlers still pinned by conservative stack scanning on this Ruby'
    end
  end
end
