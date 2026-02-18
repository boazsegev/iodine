# frozen_string_literal: true

require 'spec_helper'

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
  end

  # ---------------------------------------------------------------------------
  # In-reactor tests — single Iodine.start cycle
  #
  # All subscriptions are created BEFORE Iodine.start so they are registered
  # in the STL layer immediately. Publishes are scheduled via run_after inside
  # on_state(:start). A 500ms watchdog guarantees Iodine.stop.
  # ---------------------------------------------------------------------------
  SUB_RESULTS = {}  # rubocop:disable RSpec/LeakyConstantDeclaration

  # Keep subscription objects alive (stored here so GC doesn't auto-cancel)
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

    Iodine.verbosity = 0
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

      # Stagger publishes to avoid races — give subscriptions time to register
      # (subscription handles are set asynchronously by facil.io; 100ms margin)
      Iodine.run_after(150) { Iodine.publish(channel: 'sub-delivery',  message: 'hello-sub') }
      Iodine.run_after(250) { Iodine.publish(channel: 'sub-multi',     message: 'ping') }
      Iodine.run_after(350) { Iodine.publish(channel: 'sub-swap',      message: 'swapped') }
      Iodine.run_after(450) { Iodine.publish(channel: 'sub-cancelled', message: 'should-not-arrive') }

      # Watchdog — fires after all publishes have had time to deliver
      Iodine.run_after(800) { Iodine.stop }
    end

    Iodine.start
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
  end
end
