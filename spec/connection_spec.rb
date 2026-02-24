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
    $stdout.puts "DEBUG: before(:context) start"; $stdout.flush
    Iodine.verbosity = 2  # floor: ERROR+FATAL visible in CI
    Iodine.workers   = 0
    Iodine.threads   = 1   # raw IO needs only one worker thread
    $stdout.puts "DEBUG: config done, registering on_state"; $stdout.flush

    run_tests = proc do
      $stdout.puts "DEBUG: run_tests start"; $stdout.flush
      TCPSocket.open('127.0.0.1', RAW_PORT) do |sock|
        $stdout.puts "DEBUG: TCPSocket connected"; $stdout.flush
        # Read the server greeting
        greeting = sock.gets
        $stdout.puts "DEBUG: got greeting: #{greeting.inspect}"; $stdout.flush
        RAW_RESULTS[:greeting] = greeting&.chomp

        # Send a line and read the echo
        sock.write("PING\n")
        sock.flush
        $stdout.puts "DEBUG: sent PING"; $stdout.flush
        echo = sock.gets
        $stdout.puts "DEBUG: got echo: #{echo.inspect}"; $stdout.flush
        RAW_RESULTS[:echo] = echo&.chomp
      end
      $stdout.puts "DEBUG: run_tests done"; $stdout.flush
    rescue => e
      $stdout.puts "DEBUG: run_tests error: #{e.class}: #{e.message}"; $stdout.flush
      RAW_RESULTS[:error] = "#{e.class}: #{e.message}"
    ensure
      $stdout.puts "DEBUG: run_tests ensure, stopping"; $stdout.flush
      Iodine.run_after(100) { Iodine.stop }
    end

    Iodine.on_state(:start) do
      $stdout.puts "DEBUG: on_state(:start) fired"; $stdout.flush
      next if RAW_STARTED[0]
      RAW_STARTED[0] = true
      Iodine.run_after(500) do
        $stdout.puts "DEBUG: run_after(500) fired"; $stdout.flush
        Iodine.async do
          $stdout.puts "DEBUG: inside async block"; $stdout.flush
          run_tests.call
        end
        $stdout.puts "DEBUG: async dispatched"; $stdout.flush
      end
      Iodine.run_after(5000) { Iodine.stop }
    end

    $stdout.puts "DEBUG: calling Iodine.start"; $stdout.flush
    Iodine.start
    $stdout.puts "DEBUG: Iodine.start returned"; $stdout.flush
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
