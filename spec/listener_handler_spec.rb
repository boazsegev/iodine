# frozen_string_literal: true

require 'spec_helper'
require 'socket'

RSpec.describe 'Iodine.listen handler preparation' do
  def listen_with(handler)
    port = TCPServer.open('127.0.0.1', 0) { |server| server.addr[1] }
    Iodine.listen(url: "http://127.0.0.1:#{port}", handler: handler)
  end

  def handler_with(*callbacks)
    Object.new.tap do |handler|
      callbacks.each { |name| handler.define_singleton_method(name) { |*_| nil } }
    end
  end

  it 'provides a wrapper when a frozen handler lacks callbacks' do
    handler = handler_with(:on_http).freeze

    callback_handler = listen_with(handler).map

    expect(callback_handler).not_to equal(handler)
    expect(callback_handler).to respond_to(:on_http, :on_finish)
  end

  it 'uses a frozen handler unchanged when every callback is available' do
    handler = handler_with(:on_http, :on_finish, :on_close, :on_eventsource,
                           :on_eventsource_reconnect, :on_drained, :on_shutdown,
                           :on_timeout, :on_authenticate_sse,
                           :on_authenticate_websocket, :on_open, :on_message).freeze

    expect(listen_with(handler).map).to equal(handler)
  end

  it 'uses a mutable handler unchanged and installs its missing callbacks' do
    handler = handler_with(:on_http)

    expect(listen_with(handler).map).to equal(handler)
    expect(handler).to respond_to(:on_finish, :on_close)
  end
end
