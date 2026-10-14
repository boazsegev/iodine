# frozen_string_literal: true

require 'spec_helper'

RSpec.describe 'frozen handler preparation' do
  def prepare(handler)
    Iodine::Base.add_missing_handler_methods(handler)
  end

  it 'wraps a frozen Rack object and forwards call to the original' do
    handler = Object.new
    handler.define_singleton_method(:call) { |_env| [200, {}, ['ok']] }
    handler.freeze

    prepared = prepare(handler)

    expect(prepared).not_to equal(handler)
    expect(prepared.call({})).to eq([200, {}, ['ok']])
    expect(prepared).to respond_to(:on_http, :on_finish)
  end

  it 'retains callbacks and authentication behavior of a frozen NeoRack handler' do
    handler = Object.new
    handler.define_singleton_method(:on_http) { |_event| :original_http }
    handler.define_singleton_method(:on_authenticate) { |_event| true }
    handler.define_singleton_method(:on_message) { |_event, data| data }
    handler.freeze

    prepared = prepare(handler)

    expect(prepared.on_http(nil)).to eq(:original_http)
    expect(prepared.on_authenticate_sse(nil)).to be(true)
    expect(prepared.on_message(nil, :payload)).to eq(:payload)
  end

  it 'returns a frozen handler unchanged when it already has every required callback' do
    handler = Object.new
    %i[on_http on_finish on_close on_eventsource on_eventsource_reconnect
       on_drained on_shutdown on_timeout on_authenticate_sse
       on_authenticate_websocket on_open on_message].each do |callback|
      handler.define_singleton_method(callback) { |*_| nil }
    end
    handler.freeze

    expect(prepare(handler)).to equal(handler)
  end

  it 'returns the supplied handler after a connection switches callbacks' do
    handler = Object.new
    handler.define_singleton_method(:on_message) { |_event, data| data }
    handler.define_singleton_method(:custom_method) { :original }
    handler.freeze
    connection = Iodine::Connection.allocate

    connection.handler = handler

    expect(connection.handler).to equal(handler)
    expect(connection.handler.custom_method).to eq(:original)
  end

  it 'forwards both arguments to an existing eventsource callback' do
    handler = Object.new
    handler.define_singleton_method(:on_eventsource) { |event, message| [event, message] }
    handler.freeze

    expect(prepare(handler).on_eventsource(:event, :message)).to eq([:event, :message])
  end

  it 'forwards both arguments to an existing reconnect callback' do
    handler = Object.new
    handler.define_singleton_method(:on_eventsource_reconnect) { |event, id| [event, id] }
    handler.freeze

    expect(prepare(handler).on_eventsource_reconnect(:event, '7')).to eq([:event, '7'])
  end

  it 'prepares a class handler without modifying the frozen class' do
    handler = Class.new do
      def self.call(_env) = [200, {}, ['class response']]
    end.freeze

    expect(prepare(handler).call({})[2]).to eq(['class response'])
    expect(handler).not_to respond_to(:on_http)
  end
end
