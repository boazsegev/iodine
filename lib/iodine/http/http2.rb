module Iodine
	class Http < Iodine::Protocol
		class Http2 < ::Iodine::Protocol
			def initialize io, request = nil
				super(io)
				return unless request
				# ...
			end
			def on_open
				set_timeout 25
			end
			def on_message data
				
			end

			def send_response response
				
			end
			def stream_response response, finish = false
			end

			protected
			def parse data
				
			end
			def dispatch request
				
			end
		end
	end
end


