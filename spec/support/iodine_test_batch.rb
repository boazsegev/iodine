# frozen_string_literal: true

# IodineTestBatch — runs multiple test scenarios in a single Iodine.start/stop cycle.
#
# Usage pattern (inside an RSpec describe group):
#
#   BATCH = IodineTestBatch.new
#
#   before(:context) do
#     # Register all test work upfront
#     BATCH.test(:my_key) { |r| r[:value] = 42 }
#     BATCH.test(:other)  { |r| r[:msg] = "hello" }
#     BATCH.start!           # single Iodine.start — blocks until all done
#   end
#
#   it "checks my_key" do
#     expect(BATCH[:my_key][:value]).to eq(42)
#   end
#
# Each test block receives its own result Hash `r`.
# Blocks are run sequentially via run_after timers, STEP_MS apart.
# A watchdog fires after all slots to guarantee Iodine.stop.
#
class IodineTestBatch
  # Milliseconds between each test slot — enough for async work to settle
  STEP_MS     = 80
  # Extra ms after last test before the watchdog fires Iodine.stop
  WATCHDOG_MS = 200

  def initialize(workers: 0, threads: 1)
    @workers = workers
    @threads = threads
    @tests   = []   # [{key:, block:}]
    @results = {}   # key => Hash populated during run
  end

  # Register a test work block, keyed by +key+.
  # The block receives a fresh Hash `r` and runs inside the reactor.
  def test(key, &block)
    raise ArgumentError, "duplicate test key: #{key}" if @results.key?(key)
    @results[key] = {}
    @tests << { key: key, block: block }
    self
  end

  # Start Iodine once, run all registered tests, stop, return results.
  def start!(extra_setup: nil)
    Iodine.verbosity = 0
    Iodine.workers   = @workers
    Iodine.threads   = @threads

    tests   = @tests
    results = @results

    # Guard: on_state(:start) callbacks persist across reactor restarts.
    # This flag ensures the batch only runs once even if the reactor is
    # restarted by a later spec (which re-fires all accumulated callbacks).
    batch_started = false

    Iodine.on_state(:start) do
      next if batch_started
      batch_started = true

      # Allow any extra per-run setup (e.g. listener binding) to settle
      extra_setup&.call

      tests.each_with_index do |t, i|
        delay = (i + 1) * STEP_MS
        Iodine.run_after(delay) { t[:block].call(results[t[:key]]) }
      end

      # Watchdog fires after the last test slot + buffer
      watchdog_delay = (tests.size + 1) * STEP_MS + WATCHDOG_MS
      Iodine.run_after(watchdog_delay) { Iodine.stop }
    end

    Iodine.start
    self
  end

  # Access collected results for a test key.
  def [](key)
    @results.fetch(key) { raise KeyError, "no test registered for key: #{key.inspect}" }
  end
end
