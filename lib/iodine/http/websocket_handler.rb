require 'iodine'
require 'stringio'
require 'time'
require 'json'
require 'yaml'
require 'uri'
require 'tmpdir'
require 'zlib'

# require 'securerandom'

module Iodine
	class Http < Iodine::Protocol
		class WebsocketEchoDemo
			def initialize request, response
				@request = request
				@response = response
				on_open
			end
			def on_open
			end
			def on_message data
				@response << "You >> #{data}"
			end
			def on_close
			end

			# This method allows the class itself to act as the Websocket handler, usable with:
			#        Iodine::Http.on_websocket Iodine::Http::WebsocketEchoDemo
			def self.call request, response
				return false if request[:path] =~ /refuse/i
				self.new request, response
			end
			
			protected
		end
	end
end


