module Iodine
	class Http < Iodine::Protocol
		class Websockets < ::Iodine::Protocol
			def initialize io, handler
				@handler = handler
				super(io)
			end
			def on_open
				set_timeout 45
			end
			def on_message data
				
			end

			def send_response response
				
			end
			def stream_response response, finish = false
			end

			def self.handshake request, response, handler
				
			end
			def self.client_handshake io, handler
				
			end

			protected
			def parse data
				
			end
			def dispatch frame
				
			end
		end
	end
end


