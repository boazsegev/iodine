# frozen_string_literal: true

require 'spec_helper'
require 'socket'
require 'timeout'

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
RAW_SOCKET_TIMEOUT = 5

module RawTcpTestClient
  module_function

  def read_line(socket)
    return socket.gets if IO.select([socket], nil, nil, RAW_SOCKET_TIMEOUT)

    raise Timeout::Error, "timed out waiting for TCP response after #{RAW_SOCKET_TIMEOUT}s"
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

    raw_finished = false
    run_tests = proc do
      Iodine::Logger.debug "run_tests start"
      TCPSocket.open('127.0.0.1', RAW_PORT) do |sock|
        Iodine::Logger.debug "TCPSocket connected"
        # Read the server greeting
        greeting = RawTcpTestClient.read_line(sock)
        Iodine::Logger.debug "got greeting: #{greeting.inspect}"
        RAW_RESULTS[:greeting] = greeting&.chomp

        # Send a line and read the echo
        sock.write("PING\n")
        sock.flush
        Iodine::Logger.debug "sent PING"
        echo = RawTcpTestClient.read_line(sock)
        Iodine::Logger.debug "got echo: #{echo.inspect}"
        RAW_RESULTS[:echo] = echo&.chomp
      end
      Iodine::Logger.debug "run_tests done"
    rescue => e
      Iodine::Logger.error "run_tests error: #{e.class}: #{e.message}"
      RAW_RESULTS[:error] = "#{e.class}: #{e.message}"
    ensure
      raw_finished = true
      Iodine::Logger.debug "run_tests ensure, stopping"
      Iodine.run_after(100) { Iodine.stop }
    end

    Iodine.on_state(:start) do
      Iodine::Logger.debug "on_state(:start) fired"
      next if RAW_STARTED[0]
      RAW_STARTED[0] = true
      Iodine.run_after(500) do
        Iodine::Logger.debug "run_after(500) fired"
        # The client performs blocking reads, so it must not consume Iodine's
        # one-worker async pool. On Windows that otherwise stalls this test.
        Thread.new { run_tests.call }
        Iodine::Logger.debug "TCP client thread started"
      end
      # Timers survive reactor restarts, so an obsolete watchdog must not stop
      # a later spec's reactor cycle after this test completes normally.
      Iodine.run_after(5000) { Iodine.stop unless raw_finished }
    end

    Iodine::Logger.debug "calling Iodine.start"
    Iodine.start
    Iodine::Logger.debug "Iodine.start returned"
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
