# frozen_string_literal: true

require 'spec_helper'
require 'open3'

# =============================================================================
# Iodine::PubSub::Subscription Tests
#
# Subscription.new creates an independent non-IO-bound subscriber context.
# Unlike Iodine.subscribe (one per channel per context), multiple
# Subscription.new calls on the same channel ALL receive the message.
#
# Critical lifecycle rule:
#   Subscriptions are created BEFORE Iodine.start — they register immediately
#   in the STL layer. Message delivery is deferred through the IO reactor, so
#   callbacks only fire while Iodine.start is running.
#
# Pattern: create subs → Iodine.start → publish via run_after → collect →
#          stop via watchdog → read results after start returns.
# =============================================================================
RSpec.describe 'Iodine::PubSub::Subscription' do
  # ---------------------------------------------------------------------------
  # Static / no-reactor tests (run synchronously, no Iodine.start needed)
  # ---------------------------------------------------------------------------
  describe 'class definition' do
    it 'is defined as a Class' do
      expect(Iodine::PubSub::Subscription).to be_a(Class)
    end

    it 'raises ArgumentError when no block is given' do
      expect { Iodine::PubSub::Subscription.new('sub-noblock') }.to raise_error(ArgumentError)
    end

    it 'rejects repeated initialization without orphaning the first subscription' do
      sub = Iodine::PubSub::Subscription.allocate
      sub.send(:initialize, 'sub-reinitialize') { |_msg| }
      expect { sub.send(:initialize, 'sub-reinitialize-again') { |_msg| } }
        .to raise_error(RuntimeError, 'Subscription is already initialized')
      expect(sub.cancel).to equal(sub)
    end

    it 'remains safe when facil.io rejects a subscription synchronously' do
      sub = Iodine::PubSub::Subscription.new('x' * 65_536) { |_msg| }
      expect(sub.active?).to be false
      expect(sub.cancel).to equal(sub)
      expect { sub.handler = proc { |_msg| } }.not_to raise_error
    end

    it 'releases an active subscription during process exit' do
      child = <<~RUBY
        require 'iodine'
        Iodine::Logger.level = 5
        Iodine::PubSub::Subscription.new('sub-at-exit') { |_msg| }
      RUBY
      _stdout, stderr, status = Open3.capture3(RbConfig.ruby, '-Ilib', '-e', child)

      expect(status).to be_success
      expect(stderr).not_to match(/leaks detected for (?:iodine_pubsub_sub|fio_pubsub_subscription)/)
    end
  end

  # ---------------------------------------------------------------------------
  # In-reactor tests — single Iodine.start cycle
  #
  # All subscriptions are created BEFORE Iodine.start so they are registered
  # in the STL layer immediately. Publishes are scheduled via run_after inside
  # on_state(:start). A 500ms watchdog guarantees Iodine.stop.
  # ---------------------------------------------------------------------------
  SUB_RESULTS = {}  # rubocop:disable RSpec/LeakyConstantDeclaration

  # Keep references for subscriptions whose methods are used after creation.
  SUB_REFS = {}  # rubocop:disable RSpec/LeakyConstantDeclaration

  before(:context) do
    r = SUB_RESULTS

    # -------------------------------------------------------------------------
    # Test: active? is true after creation (checked before reactor starts)
    # -------------------------------------------------------------------------
    r[:active_before_start] = {}
    sub_active = Iodine::PubSub::Subscription.new('sub-active-check') { |_msg| }
    r[:active_before_start][:active] = sub_active.active?
    sub_active.cancel  # clean up immediately — no reactor needed

    # -------------------------------------------------------------------------
    # Test: active? is false after cancel (checked before reactor starts)
    # -------------------------------------------------------------------------
    r[:inactive_after_cancel] = {}
    sub_cancel = Iodine::PubSub::Subscription.new('sub-cancel-check') { |_msg| }
    sub_cancel.cancel
    r[:inactive_after_cancel][:active] = sub_cancel.active?

    # -------------------------------------------------------------------------
    # Test: cancel is idempotent (no reactor needed)
    # -------------------------------------------------------------------------
    r[:cancel_idempotent] = { ok: nil, error: nil }
    sub_idem = Iodine::PubSub::Subscription.new('sub-idem') { |_msg| }
    begin
      sub_idem.cancel
      sub_idem.cancel
      r[:cancel_idempotent][:ok] = true
    rescue => e
      r[:cancel_idempotent][:error] = e.message
      r[:cancel_idempotent][:ok] = false
    end

    # -------------------------------------------------------------------------
    # Test: handler getter returns the Proc (no reactor needed)
    # -------------------------------------------------------------------------
    r[:handler_getter] = {}
    sub_hget = Iodine::PubSub::Subscription.new('sub-hget') { |_msg| }
    r[:handler_getter][:handler_class] = sub_hget.handler.class
    r[:handler_getter][:handler_nil]   = sub_hget.handler.nil?
    sub_hget.cancel

    # -------------------------------------------------------------------------
    # Test: a global subscription stays active without a caller-held reference
    # -------------------------------------------------------------------------
    r[:unreferenced_global] = { received: false }
    create_global_subscription = lambda do
      Iodine::PubSub::Subscription.new('sub-unreferenced-global') do |_msg|
        r[:unreferenced_global][:received] = true
      end
      nil
    end
    create_global_subscription.call
    GC.start(full_mark: true, immediate_sweep: true)

    # -------------------------------------------------------------------------
    # Start the reactor — create delivery subs inside on_state(:start),
    # publish via run_after, watchdog stops it.
    #
    # NOTE: Subscriptions CAN be created before start (they register in the
    # STL layer immediately), but for test reliability with the thread pool
    # queue, we create delivery-dependent subs inside on_state(:start) where
    # the reactor is guaranteed live. The active?/cancel/handler tests above
    # use subscriptions created before start — that works because they don't
    # depend on message delivery.
    # -------------------------------------------------------------------------
    r[:message_delivery]      = { msg: nil }
    r[:multi_independent]     = { count: 0 }
    r[:handler_swap]          = { old_fired: false, new_fired: false, new_msg: nil }
    r[:cancelled_no_delivery] = { received: false }
    r[:queued_cancel]         = { received: false }
    queued_cancel_gate = Queue.new
    queued_cancel_thread = nil

    Iodine.workers   = 0
    Iodine.threads   = 1

    # Guard: on_state(:start) callbacks persist across reactor restarts.
    # This flag ensures the subscription setup only runs once.
    sub_started = false

    Iodine.on_state(:start) do
      next if sub_started
      sub_started = true

      # Create delivery-dependent subscriptions now that the reactor is live
      SUB_REFS[:delivery] = Iodine::PubSub::Subscription.new('sub-delivery') do |msg|
        r[:message_delivery][:msg] = msg.message
      end

      SUB_REFS[:multi1] = Iodine::PubSub::Subscription.new('sub-multi') { |_msg| r[:multi_independent][:count] += 1 }
      SUB_REFS[:multi2] = Iodine::PubSub::Subscription.new('sub-multi') { |_msg| r[:multi_independent][:count] += 1 }

      SUB_REFS[:swap] = Iodine::PubSub::Subscription.new('sub-swap') { |_msg| r[:handler_swap][:old_fired] = true }
      new_handler = proc { |_msg| r[:handler_swap][:new_fired] = true }
      SUB_REFS[:swap].handler = new_handler
      r[:handler_swap][:getter_updated] = (SUB_REFS[:swap].handler == new_handler)

      sub_cancelled = Iodine::PubSub::Subscription.new('sub-cancelled') { |_msg| r[:cancelled_no_delivery][:received] = true }
      sub_cancelled.cancel

      SUB_REFS[:queue_blocker] = Iodine::PubSub::Subscription.new('sub-queue-blocker') do |_msg|
        queued_cancel_gate.pop
      end
      SUB_REFS[:queued_cancel] = Iodine::PubSub::Subscription.new('sub-queued-cancel') do |_msg|
        r[:queued_cancel][:received] = true
      end

      # Stagger publishes to avoid races — give subscriptions time to register
      # (subscription handles are set asynchronously by facil.io; 100ms margin)
      Iodine.run_after(150) { Iodine.publish(channel: 'sub-delivery',  message: 'hello-sub') }
      Iodine.run_after(250) { Iodine.publish(channel: 'sub-multi',     message: 'ping') }
      Iodine.run_after(350) { Iodine.publish(channel: 'sub-swap',      message: 'swapped') }
      Iodine.run_after(450) { Iodine.publish(channel: 'sub-cancelled', message: 'should-not-arrive') }
      Iodine.run_after(550) { Iodine.publish(channel: 'sub-unreferenced-global', message: 'still-active') }
      Iodine.run_after(650) do
        Iodine.publish(channel: 'sub-queue-blocker', message: 'block-worker')
        Iodine.publish(channel: 'sub-queued-cancel', message: 'already-queued')
        queued_cancel_thread = Thread.new do
          sleep 0.05
          SUB_REFS[:queued_cancel].cancel
          queued_cancel_gate << true
        end
      end

      # Watchdog — fires after all publishes have had time to deliver
      Iodine.run_after(1_100) { Iodine.stop }
    end

    Iodine.start
    queued_cancel_thread&.join
  end

  # ---------------------------------------------------------------------------
  # active? — synchronous checks (no reactor needed)
  # ---------------------------------------------------------------------------
  describe '#active?' do
    it 'returns true immediately after creation (before reactor)' do
      expect(SUB_RESULTS[:active_before_start][:active]).to be true
    end

    it 'returns false after cancel' do
      expect(SUB_RESULTS[:inactive_after_cancel][:active]).to be false
    end
  end

  # ---------------------------------------------------------------------------
  # cancel idempotency
  # ---------------------------------------------------------------------------
  describe '#cancel' do
    it 'is idempotent — calling twice raises no error' do
      expect(SUB_RESULTS[:cancel_idempotent][:ok]).to be true
      expect(SUB_RESULTS[:cancel_idempotent][:error]).to be_nil
    end
  end

  # ---------------------------------------------------------------------------
  # handler getter
  # ---------------------------------------------------------------------------
  describe '#handler' do
    it 'returns a Proc (not nil) while active' do
      expect(SUB_RESULTS[:handler_getter][:handler_nil]).to be false
      expect(SUB_RESULTS[:handler_getter][:handler_class]).to eq(Proc)
    end
  end

  # ---------------------------------------------------------------------------
  # Message delivery
  # ---------------------------------------------------------------------------
  describe 'message delivery' do
    it 'delivers the published message to the subscriber' do
      expect(SUB_RESULTS[:message_delivery][:msg]).to eq('hello-sub')
    end
  end

  # ---------------------------------------------------------------------------
  # Multiple independent subscribers
  # ---------------------------------------------------------------------------
  describe 'multiple independent subscribers on the same channel' do
    it 'delivers the message to ALL subscribers (count == 2)' do
      expect(SUB_RESULTS[:multi_independent][:count]).to eq(2)
    end
  end

  # ---------------------------------------------------------------------------
  # handler= — updates the getter; subscribed proc (udata) is fixed at new time
  # ---------------------------------------------------------------------------
  describe '#handler=' do
    it 'updates the handler getter to the new proc' do
      expect(SUB_RESULTS[:handler_swap][:getter_updated]).to be true
    end

    it 'the new proc receives messages after handler= (udata updated to new proc)' do
      expect(SUB_RESULTS[:handler_swap][:new_fired]).to be true
    end
  end

  # ---------------------------------------------------------------------------
  # Cancelled subscription does not receive messages
  # ---------------------------------------------------------------------------
  describe 'cancelled subscription' do
    it 'does not receive messages after cancel' do
      expect(SUB_RESULTS[:cancelled_no_delivery][:received]).to be false
    end

    it 'keeps a global subscription alive without a caller-held reference' do
      expect(SUB_RESULTS[:unreferenced_global][:received]).to be true
    end

    it 'does not invoke a handler that was queued before cancellation' do
      expect(SUB_RESULTS[:queued_cancel][:received]).to be false
    end
  end
end
