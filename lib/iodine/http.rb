require 'iodine'
require 'stringio'
require 'time'
require 'json'
require 'yaml'
require 'uri'
require 'tmpdir'
require 'zlib'
require 'securerandom'

require 'iodine/http/request'
require 'iodine/http/response'
require 'iodine/http/session'

require 'iodine/http/http1'

require 'iodine/http/hpack'
require 'iodine/http/http2'

require 'iodine/http/websockets'
require 'iodine/http/websocket_handler'
require 'iodine/http/websocket_client'

require 'iodine/http/rack_support'


module Iodine

	# The {Iodine::Http} class allows the creation of Http and Websocket servers using Iodine.
	#
	# To start an Http server, simply require `iodine/http` (which isn't required by default) and set up
	# your Http callback. i.e.:
	#
	#       require 'iodine/http'
	#       Iodine::Http.on_http { |request, response| 'Hello World!' }
	#
	# To start a Websocket server, require `iodine/http` (which isn't required by default), create a Websocket handling Class and set up
	# your Websocket callback. i.e.:
	#
	#       require 'iodine/http'
	#       class WSChatServer
	#          def initialize nickname, response
	#              @nickname = nickname || "unknown"
	#              @response = response
	#              # @response.io # => Http Protocol
	#          end
	#          def on_open
	#              # only now is the response.io pointing at the Websocket Protocol
	#              @io = @response.io
	#              @io.broadcast "#{@nickname} has joined the chat!"
	#              @io << "Welcome #{@nickname}, you have joined the chat!"
	#          end
	#          def on_message data
	#              @io.broadcast "#{@nickname} >> #{data}"
	#              @io << ">> #{data}"
	#          end
	#          def on_broadcast data
	#              # the http response can also be used to send websocket data.
	#              @response << data
	#          end
	#          def on_close
	#              @io.broadcast "#{@nickname} has left the chat!"
	#          end
	#       end
	#
	#       Iodine::Http.on_websocket { |request, response| WSChatServer.new request.params[:name], response}
	#
	# See {Iodine::Http::WebsocketHandler} for a good starting point or inherit {Iodine::Http::WebsocketHandler} in your handler.
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

		# Creates a websocket client within a new task (non-blocking).
		# 
		# Make sure to setup all the callbacks (as needed) prior to starting the connection. See {::Iodine::Http::WebsocketClient.connect}
		#
		# i.e.:
		#
		#       require 'iodine/http'
		#       # don't start the server
		#       Iodine.protocol = :timer
		#       options = {}
		#       options[:on_open] = Proc.new { write "Hello there!"}
		#       options[:on_message] = Proc.new do |data|
		#           puts ">> #{data}";
		#           write "Bye!";
		#           # It's possible to update the callback midstream.
		#           on_message {|data| puts "-- Goodbye message: #{data}"; close;}
		#       end
		#       # After closing we will call `Iodine.signal_exit` to signal Iodine to finish up.
		#       options[:on_close] = Proc.new { puts "disconnected"; Iodine.signal_exit }
		#       
		#       Iodine::Http.ws_connect "ws://echo.websocket.org", options
		#
		#       #if running from irb:
		#       exit
		#     
		def self.ws_connect url, options={}, &block
			::Iodine.run { ::Iodine::Http::WebsocketClient.connect url, options, &block }
		end

		@websocket_app = @http_app = NOT_IMPLEMENTED = Proc.new { |i,o| false }
		@session_token = "#{File.basename($0, '.*')}_uuid"
	end

	@queue.tap do |q|
		arr =[];
		arr << q.pop until q.empty?;
		run { Iodine.ssl_protocols = { 'h2' => Iodine::Http::Http2, 'http/1.1' => Iodine::Http } if @ssl && @ssl_protocols.empty? }
		run do
			if Iodine.protocol == ::Iodine::Http && ::Iodine::Http.on_http == ::Iodine::Http::NOT_IMPLEMENTED && ::Iodine::Http.on_websocket == ::Iodine::Http::NOT_IMPLEMENTED
				::Iodine.protocol = :http_not_initialized
				q << arr.shift until arr.empty?
				run { Process.kill("INT", 0) }
			end
		end
		q << arr.shift until arr.empty?
	end
end
Iodine.protocol = ::Iodine::Http
