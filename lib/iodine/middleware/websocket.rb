# frozen_string_literal: true

require_relative 'websocket/upgrader'
require_relative 'websocket/handler'

module Iodine
  module Middleware
    module WebSocket
      class << self
        attr_accessor :rack_handler
      end
    end
  end
end
