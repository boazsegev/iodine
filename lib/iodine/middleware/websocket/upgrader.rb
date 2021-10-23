# frozen_string_literal: true

module Iodine
  module Middleware
    module WebSocket
      class << self
        attr_accessor :deflate_min_size
      end

      WebSocket.deflate_min_size = 1024

      class Upgrader
        WS_RESPONSE = [0, {}, []].freeze

        def initialize(app)
          @app = app
        end

        def call(env)
          return @app.call(env) unless env['rack.upgrade?']

          create_response(env)
        end

        private

        def create_response(env)
          if WebSocket.rack_handler.is_a? Class
            env['rack.upgrade'] = WebSocket.rack_handler.new
          else
            env['rack.upgtade'] = WebSocket.rack_handler
          end

          WS_RESPONSE
        end
      end

      class DeflateUpgrader < Upgrader
        WS_DEFLATE_RESPONSE =
          [0, { 'Sec-WebSocket-Extensions' => 'permessage-deflate' }, []].freeze

        def create_response(env)
          super

          return WS_RESPONSE unless env['HTTP_SEC_WEBSOCKET_EXTENSIONS']&.match?('permessage-deflate')

          env['rack.ws.deflate.min_size'] = WebSocket.deflate_min_size

          WS_DEFLATE_RESPONSE
        end
      end
    end
  end
end
