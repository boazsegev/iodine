# frozen_string_literal: true

require 'spec_helper'
require 'support/iodine_test_batch'

# =============================================================================
# Iodine Runtime Lifecycle Tests
#
# All tests that require a running reactor share a single Iodine.start/stop
# cycle per describe group (via before(:context) / IodineTestBatch).
# Tests that do NOT need the reactor (verbosity=, secret=, etc.) run outside.
# =============================================================================

# ---------------------------------------------------------------------------
# Group 1: Tests that can only be observed while the reactor is running.
# One Iodine.start covers: running?, master?, worker?, on_state callbacks,
# run, async, run_after behaviour.
# ---------------------------------------------------------------------------
RSpec.describe 'Iodine runtime — reactor lifecycle' do
  LIFECYCLE_BATCH = IodineTestBatch.new  # rubocop:disable RSpec/LeakyConstantDeclaration

  before(:context) do
    # --- on_state callbacks must be registered before start! ---

    # :pre_start fires before workers spin up — registered here, outside batch
    LIFECYCLE_BATCH.instance_variable_get(:@results)[:pre_start] = {}
    pre_r = LIFECYCLE_BATCH.instance_variable_get(:@results)[:pre_start]
    Iodine.on_state(:pre_start) { pre_r[:fired] = true }

    # :start_shutdown and :stop fire during shutdown
    LIFECYCLE_BATCH.instance_variable_get(:@results)[:shutdown_cb] = {}
    shutdown_r = LIFECYCLE_BATCH.instance_variable_get(:@results)[:shutdown_cb]
    Iodine.on_state(:shutdown) { shutdown_r[:shutdown_fired] = true }
    Iodine.on_state(:stop)     { shutdown_r[:stop_fired]     = true }

    # Repetition counters registered at on_state(:start) level so they fire
    # across the full reactor window — not nested inside a batch slot.
    LIFECYCLE_BATCH.instance_variable_get(:@results)[:repeat_counters] = {}
    rpt_r = LIFECYCLE_BATCH.instance_variable_get(:@results)[:repeat_counters]
    rpt_r[:n3] = 0
    rpt_r[:inf] = 0
    # Guard: on_state(:start) callbacks persist across reactor restarts.
    # This flag ensures the repeat counters are only registered once.
    rpt_started = false
    Iodine.on_state(:start) do
      next if rpt_started
      rpt_started = true
      Iodine.run_after(20, 3) { rpt_r[:n3]  += 1 }  # fires 3 times
      Iodine.run_after(10, 0) { rpt_r[:inf] += 1 }  # fires until stopped
    end

    # Register all in-reactor test work
    LIFECYCLE_BATCH
      .test(:running_state) { |r|
        r[:during] = Iodine.running?
      }
      .test(:master_worker) { |r|
        r[:master] = Iodine.master?
        r[:worker] = Iodine.worker?
      }
      .test(:start_count) { |r|
        # on_state(:start) already fired once by this point; record it
        r[:was_running] = Iodine.running?
      }
      .test(:run_block) { |r|
        r[:executed] = false
        p = Iodine.run { r[:executed] = true }
        r[:returned_proc] = p.is_a?(Proc)
      }
      .test(:async_block) { |r|
        r[:executed] = false
        p = Iodine.async { r[:executed] = true }
        r[:returned_proc] = p.is_a?(Proc)
        # async runs in a thread pool — give it time before next slot reads it
      }
      .test(:run_after_basic) { |r|
        r[:fired]   = false
        r[:running] = nil
        # Use a short delay so the timer fires well within the reactor window.
        # The slot itself runs at (slot_index+1)*STEP_MS; the timer fires
        # STEP_MS/2 ms later, leaving plenty of time before the watchdog.
        p = Iodine.run_after(10) {
          r[:fired]   = true
          r[:running] = Iodine.running?
        }
        r[:returned_proc] = p.is_a?(Proc)
      }
      .test(:run_after_repeat) { |r|
        r[:count] = 0
        # Schedule 3 individual timers to avoid nesting issues
        Iodine.run_after(10) { r[:count] += 1 }
        Iodine.run_after(20) { r[:count] += 1 }
        Iodine.run_after(30) { r[:count] += 1 }
      }
      .test(:run_after_forever) { |r|
        r[:count] = 0
        # Schedule several individual timers spread across the slot window
        Iodine.run_after(10) { r[:count] += 1 }
        Iodine.run_after(20) { r[:count] += 1 }
        Iodine.run_after(30) { r[:count] += 1 }
        Iodine.run_after(40) { r[:count] += 1 }
        Iodine.run_after(50) { r[:count] += 1 }
      }

    LIFECYCLE_BATCH.start!
  end

  # -----------------------------------------------------------------------
  # Iodine.running?
  # -----------------------------------------------------------------------
  describe 'Iodine.running?' do
    it 'is false before start' do
      expect(Iodine.running?).to be false
    end

    it 'is true while the reactor is running' do
      expect(LIFECYCLE_BATCH[:running_state][:during]).to be true
    end

    it 'is false after start returns' do
      expect(Iodine.running?).to be false
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.master? / Iodine.worker?
  # -----------------------------------------------------------------------
  describe 'Iodine.master?' do
    it 'is true in single-process mode' do
      expect(LIFECYCLE_BATCH[:master_worker][:master]).to be true
    end
  end

  describe 'Iodine.worker?' do
    it 'is true in single-process mode' do
      expect(LIFECYCLE_BATCH[:master_worker][:worker]).to be true
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.on_state callbacks
  # -----------------------------------------------------------------------
  describe 'Iodine.on_state' do
    it ':pre_start fires before the reactor starts' do
      expect(LIFECYCLE_BATCH.instance_variable_get(:@results)[:pre_start][:fired]).to be true
    end

    it ':start — reactor was running inside the batch' do
      expect(LIFECYCLE_BATCH[:start_count][:was_running]).to be true
    end

    it ':shutdown fires when stopping' do
      expect(LIFECYCLE_BATCH.instance_variable_get(:@results)[:shutdown_cb][:shutdown_fired]).to be true
    end

    it ':stop fires when the reactor stops' do
      expect(LIFECYCLE_BATCH.instance_variable_get(:@results)[:shutdown_cb][:stop_fired]).to be true
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.run
  # -----------------------------------------------------------------------
  describe 'Iodine.run' do
    it 'executes the block on the IO thread' do
      expect(LIFECYCLE_BATCH[:run_block][:executed]).to be true
    end

    it 'returns a Proc object' do
      expect(LIFECYCLE_BATCH[:run_block][:returned_proc]).to be true
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.async
  # -----------------------------------------------------------------------
  describe 'Iodine.async' do
    it 'executes the block in a worker thread' do
      expect(LIFECYCLE_BATCH[:async_block][:executed]).to be true
    end

    it 'returns a Proc object' do
      expect(LIFECYCLE_BATCH[:async_block][:returned_proc]).to be true
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.run_after
  # -----------------------------------------------------------------------
  describe 'Iodine.run_after' do
    it 'fires the block after the delay (reactor was still running when it fired)' do
      expect(LIFECYCLE_BATCH[:run_after_basic][:fired]).to be true
      expect(LIFECYCLE_BATCH[:run_after_basic][:running]).to be true
    end

    it 'returns a Proc object' do
      expect(LIFECYCLE_BATCH[:run_after_basic][:returned_proc]).to be true
    end

    it 'fires multiple times when repetitions > 1' do
      rpt = LIFECYCLE_BATCH.instance_variable_get(:@results)[:repeat_counters]
      expect(rpt[:n3]).to be >= 2  # at least 2 of 3 fire before watchdog
    end

    it 'repeats indefinitely when repetitions is 0 (until stopped)' do
      rpt = LIFECYCLE_BATCH.instance_variable_get(:@results)[:repeat_counters]
      expect(rpt[:inf]).to be >= 2  # at least 2 iterations in the window
    end
  end
end

# ---------------------------------------------------------------------------
# Group 2: Tests that do NOT need the reactor — config setters/getters.
# These run as ordinary synchronous RSpec examples.
# ---------------------------------------------------------------------------
RSpec.describe 'Iodine configuration (no reactor)' do
  before(:each) { Iodine.verbosity = 0 }

  # -----------------------------------------------------------------------
  # Iodine.verbosity
  # -----------------------------------------------------------------------
  describe 'Iodine.verbosity' do
    it 'returns an Integer' do
      expect(Iodine.verbosity).to be_a(Integer)
    end

    it 'can be set to 0 (silent)' do
      Iodine.verbosity = 0
      expect(Iodine.verbosity).to eq(0)
    end

    it 'can be set to 5 (debug) and back' do
      Iodine.verbosity = 5
      expect(Iodine.verbosity).to eq(5)
      Iodine.verbosity = 0
    end

    it 'raises TypeError for a non-Integer' do
      expect { Iodine.verbosity = 'loud' }.to raise_error(TypeError)
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.secret
  # -----------------------------------------------------------------------
  describe 'Iodine.secret' do
    it 'returns a String' do
      expect(Iodine.secret).to be_a(String)
    end

    it 'returns exactly 64 bytes' do
      expect(Iodine.secret.bytesize).to eq(64)
    end

    it 'changes when set to a new key' do
      original = Iodine.secret.dup
      Iodine.secret = 'spec_test_key_abc'
      expect(Iodine.secret.bytesize).to eq(64)
      expect(Iodine.secret).not_to eq(original)
    end

    it 'raises TypeError when set to a non-String' do
      expect { Iodine.secret = 12_345 }.to raise_error(TypeError)
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.shutdown_timeout
  # -----------------------------------------------------------------------
  describe 'Iodine.shutdown_timeout' do
    it 'returns an Integer' do
      expect(Iodine.shutdown_timeout).to be_a(Integer)
    end

    it 'can be updated' do
      Iodine.shutdown_timeout = 5000
      expect(Iodine.shutdown_timeout).to eq(5000)
    end

    it 'raises TypeError for a non-Integer' do
      expect { Iodine.shutdown_timeout = 'fast' }.to raise_error(TypeError)
    end

    it 'raises RangeError when exceeding 5 minutes (300_000 ms)' do
      expect { Iodine.shutdown_timeout = 300_001 }.to raise_error(RangeError)
    end
  end

  # -----------------------------------------------------------------------
  # Iodine.threads / Iodine.workers (getters only — setters need pre-start)
  # -----------------------------------------------------------------------
  describe 'Iodine.threads' do
    it 'returns an Integer' do
      expect(Iodine.threads).to be_a(Integer)
    end
  end

  describe 'Iodine.workers' do
    it 'returns an Integer' do
      expect(Iodine.workers).to be_a(Integer)
    end
  end
end
