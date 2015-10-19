require 'iodine'
require 'stringio'
require 'time'
require 'json'
require 'yaml'
require 'uri'
require 'tmpdir'
require 'zlib'

# require 'securerandom'

require 'iodine/http/request'
require 'iodine/http/response'
require 'iodine/http/session'

require 'iodine/http/http1'

require 'iodine/http/hpack'
require 'iodine/http/http2'

require 'iodine/http/websockets'
# require 'iodine/http/websockets_handler'


module Iodine

	# The {Iodine::Http} class allows the creation of Http and Websocket servers using Iodine.
	#
	# To start an Http server, simply require `iodine/http` (which isn't required by default) and set up
	# your Http callback. i.e.:
	#
	#       require 'iodine/http'
	#       Iodine::Http.on_http { 'Hello World!' }
	#
	# To start a Websocket server, require `iodine/http` (which isn't required by default), create a Websocket handling Class and set up
	# your Websocket callback. i.e.:
	#
	#       require 'iodine/http'
	#       class WSEcho
	#          def initialize http_request
	#              @request = http_request
	#              @io = http_request.io # this is the Websockets Protocol object.
	#          end
	#          def on_open
	#          end
	#          def on_message data
	#              @response << ">> #{data}"
	#          end
	#          def on_close
	#          end
	#       end
	#
	#       Iodine::Http.on_websocket { |request| WSEcho.new(request) }
	#
	class Http < Iodine::Protocol
		# Sets or gets the Http callback.
		#
		# An Http callback is a Proc like object that answers to `call(request, response)` and returns either:
		# `true`:: the response has been set by the callback and can be managed (including any streaming) by the server.
		# `false`:: the request shouldn't be answered or resource not found (error 404 will be sent as a response).
		# String:: the String will be appended to the response and the response sent.
		def self.on_http handler = nil, &block
			@http_app = handler || block if handler || block
			@http_app
		end
		# Sets or gets the Websockets callback.
		#
		# A Websockets callback is a Proc like object that answers to `call(request)` and returns either:
		# `false`:: the request shouldn't be answered or resource not found (error 404 will be sent as a response).
		# Websocket Handler:: a Websocket handler is an object that is expected to answer `on_message(data)` and `on_close`. See {} for more data.
		def self.on_websocket handler = nil, &block
			@websocket_app = handler || block if handler || block
			@websocket_app
		end

		# Sets the session token for the Http server (String). Defaults to the name of the script + '_id'.
		def self.session_token= token
			@session_token = token
		end
		# Sets the session token for the Http server (String). Defaults to the name of the script.
		def self.session_token
			@session_token
		end

		@websocket_app = @http_app = Proc.new { |i,o| false }
		@session_token = "#{File.basename($0, '.*')}_uuid"
	end
end
Iodine.protocol = ::Iodine::Http
Iodine.run {Iodine.logger << "Iodine's Http server is setup to run.\n"}
Iodine.ssl_protocols = { 'h2' => Iodine::Http::Http2, 'http/1.1' => Iodine::Http }
