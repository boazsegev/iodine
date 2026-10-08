# frozen_string_literal: true

require 'spec_helper'

# =============================================================================
# Single Reactor Cycle (Minimal Teardown Repro)
#
# Diagnostic spec for the Windows `win32_mutex_lock: WAIT_ABANDONED` crash
# seen at reactor teardown with `Iodine.threads = 1`.
#
# Deliberately minimal: no listeners, no connections, no handlers, no client
# sockets - a single `Iodine.start` / `Iodine.stop` cycle with one async pool
# thread. Interpretation:
#
# - CRASHES: every higher-level suspect (raw TCP, HTTP, pub/sub, TLS,
#   watchdog timers, spec-side mistakes) is exonerated by construction; the
#   defect is in the core pool-thread lifecycle on Windows.
# - PASSES: the delta between this spec and connection_spec.rb (listener,
#   connections, handler callbacks, watchdog) IS the remaining search space.
#
# Suite-safe: the on_state(:start) callback fires exactly once (guarded), and
# both timers are single-shot, so later reactor cycles are never affected.
# =============================================================================

RSpec.describe 'single reactor cycle (minimal teardown repro)' do
  SINGLE_CYCLE_STATE = { started: false, finished: false } # rubocop:disable RSpec/LeakyConstantDeclaration

  before(:context) do
    Iodine::Logger.debug 'single-cycle: configuring workers=0, threads=1'
    Iodine.workers   = 0
    Iodine.threads   = 1

    Iodine.on_state(:start) do
      next if SINGLE_CYCLE_STATE[:started]

      SINGLE_CYCLE_STATE[:started] = true
      Iodine::Logger.debug 'single-cycle: reactor started, scheduling stop'
      Iodine.run_after(250) do
        Iodine::Logger.debug 'single-cycle: stop timer fired'
        SINGLE_CYCLE_STATE[:finished] = true
        Iodine.stop
      end
      # Timers survive reactor restarts, so an obsolete watchdog must not
      # stop a later spec's reactor cycle after this one completes normally.
      Iodine.run_after(10_000) { Iodine.stop unless SINGLE_CYCLE_STATE[:finished] }
    end

    Iodine::Logger.debug 'single-cycle: calling Iodine.start'
    Iodine.start
    Iodine::Logger.debug 'single-cycle: Iodine.start returned'
  end

  it 'completes one start/stop cycle' do
    expect(SINGLE_CYCLE_STATE[:started]).to be(true)
    expect(SINGLE_CYCLE_STATE[:finished]).to be(true)
  end
end
