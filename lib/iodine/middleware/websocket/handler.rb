# frozen_string_literal: true

module Iodine
  module Middleware
    module WebSocket
      class Handler
        def self.inherited(subklass)
          Iodine::Middleware::WebSocket.rack_handler = subklass
        end
      end
    end
  end
end
