
module Iodine
	module Http
		# Create a simple Websocket Client(!).
		#
		# This should be done from within an Iodine task, or the callbacks will not be called.
		#
		# Use {Iodine::Http::WebsocketClient.connect} to initialize a client with all the callbacks needed.
		class WebsocketClient
		
			attr_accessor :response, :request, :params

			def initialize options
				@response = nil
				@options = options
				@on_message = @options[:on_message]
				raise "Websocket client must have an #on_message Proc or handler." unless @on_message && @on_message.respond_to?(:call)
				@on_open = @options[:on_open]
				@on_close = @options[:on_close]
				@on_error = @options[:on_error]
				@renew = @options[:renew].to_i
				@options[:url] = URI.parse(@options[:url]) unless @options[:url].is_a?(URI)
				@connection_lock = Mutex.new
				raise TypeError, "Websocket Client `:send` should be either a String or a Proc object." if @options[:send] && !(@options[:send].is_a?(String) || @options[:send].is_a?(Proc))
				on_close && (@io || raise("Connection error, cannot create websocket client")) unless connect
			end

			def on event_name, &block
				return false unless block
				case event_name
				when :message
					@on_message = block
				when :close
					@on_close = block
				when :open
					raise 'The on_open even is invalid at this point.'
				end
											
			end

			def on_message(data = nil, &block)
				unless data
					@on_message = block if block
					return @on_message
				end
				begin
					instance_exec( data, &@on_message) 
				rescue => e
					@on_error ? @on_error.call(e) : raise(e)
				end
			end

			def on_open
				raise 'The on_open even is invalid at this point.' if block_given?
				@renew = @options[:renew].to_i
				@io = @request[:io]
				Iodine::Http::Request.parse @request
				begin
					instance_exec(&@on_open) if @on_open
				rescue => e
					@on_error ? @on_error.call(e) : raise(e)
				end
				if @options[:every] && @options[:send]
					Iodine.run_every @options[:every], self, @options do |ws, client_params, timer|
						if ws.closed?
							timer.stop!
							next
						end
						if client_params[:send].is_a?(String)
							ws.write client_params[:send]
						elsif client_params[:send].is_a?(Proc)
							begin
								ws.instance_exec(&client_params[:send])
							rescue => e
								@on_error ? @on_error.call(e) : raise(e)
							end							
						end
					end
				end
			end

			def on_close(&block)
				return @on_close = block if block
				if @renew > 0
					renew_proc = Proc.new do
						@io = nil
						begin
							raise unless connect
						rescue
							@renew -= 1
							if @renew <= 0
								Iodine.fatal "WebsocketClient renewal FAILED for #{@options[:url]}"
								on_close
							else
								Iodine.warn "WebsocketClient renewal failed for #{@options[:url]}, #{@renew} attempts left"
								renew_proc.call
							end
							false
						end
					end
					@connection_lock.synchronize { renew_proc.call }
				else
					begin
						instance_exec(&@on_close) if @on_close
					rescue => e
						@on_error ? @on_error.call(e) : raise(e)
					end
				end
			end
			def on_error(error = nil, &block)
				return @on_error = block if block
				instance_exec(error, &@on_error) if @on_error
				on_close unless @io # if the connection was initialized, :on_close will be called by Iodine
			end

			def on_shutdown
				@renew = 0
			end

			# closes the connection, if open
			def close
				@renew = 0
				@io.close if @io
			end

			# checks if the socket is open (if the websocket was terminated abnormally, this might return true for a while after it should be false).
			def closed?
				(@io && @io.io) ? @io.io.closed? : true
			end

			# checks if this is an SSL websocket connection.
			def ssl?
				@request.ssl?
			end

			# return a Hash with the HTTP cookies recieved during the HTTP's handshake.
			def cookies
				@request.cookies
			end

			# Sends data through the websocket, after client side masking.
			#
			# @return [true, false] Returns the true if the data was actually sent or nil if no data was sent.
			def write data, op_code = nil, fin = true, ext = 0
				return false if !data || data.empty?
				data = data.dup # needed?
				unless op_code # apply extenetions to the message as a whole
					op_code = (data.encoding == ::Encoding::UTF_8 ? 1 : 2) 
					# @ws_extentions.each { |ex| ext |= ex.edit_message data } if @ws_extentions
				end
				byte_size = data.bytesize
				if byte_size > (::Iodine::Http::Websockets::FRAME_SIZE_LIMIT+2)
					# sections = byte_size/FRAME_SIZE_LIMIT + (byte_size % ::Iodine::Http::Websockets::FRAME_SIZE_LIMIT ? 1 : 0)
					ret = write( data.slice!( 0...::Iodine::Http::Websockets::FRAME_SIZE_LIMIT ), op_code, data.empty?, ext) && (ext = op_code = 0) until data.empty?
					return ret # avoid sending an empty frame.
				end
				# @ws_extentions.each { |ex| ext |= ex.edit_frame data } if @ws_extentions
				header = ( (fin ? 0b10000000 : 0) | (op_code & 0b00001111) | ext).chr.force_encoding(::Encoding::ASCII_8BIT)

				if byte_size < 125
					header << (byte_size | 128).chr
				elsif byte_size.bit_length <= 16					
					header << 254.chr
					header << [byte_size].pack('S>'.freeze)
				else
					header << 255.chr
					header << [byte_size].pack('Q>'.freeze)
				end
				@@make_mask_proc ||= Proc.new {Random.rand(251) + 1}
				mask = Array.new(4, &(@@make_mask_proc))
				header << mask.pack('C*'.freeze)
				@connection_lock.synchronize do
					return false if @io.nil? || @io.closed?
					@io.write header
					i = -1;
					@io.write(data.bytes.map! {|b| (b ^ mask[i = (i + 1)%4]) } .pack('C*'.freeze)) && true
				end
			end
			alias :<< :write

			protected

			def connect
				return false if @io && !@io.closed?
				socket = nil
				url = @options[:url]
				@options[:renew] ||= 5 if @options[:every] && @options[:send]

				ssl = url.scheme == "https" || url.scheme == "wss"

				url.port ||= ssl ? 443 : 80
				url.path = '/' if url.path.to_s.empty?
				socket = TCPSocket.new(url.host, url.port)
				if ssl
					context = OpenSSL::SSL::SSLContext.new
					context.cert_store = OpenSSL::X509::Store.new
					context.cert_store.set_default_paths
					context.set_params verify_mode: (@options[:verify_mode] || OpenSSL::SSL::VERIFY_NONE) # OpenSSL::SSL::VERIFY_PEER #OpenSSL::SSL::VERIFY_NONE
					ssl = OpenSSL::SSL::SSLSocket.new(socket, context)
					ssl.sync_close = true
					ssl.connect
				end
				# prep custom headers
				custom_headers = String.new
				custom_headers = @options[:headers] if @options[:headers].is_a?(String)
				@options[:headers].each {|k, v| custom_headers << "#{k.to_s}: #{v.to_s}\r\n"} if @options[:headers].is_a?(Hash)
				@options[:cookies].each {|k, v| raise 'Illegal cookie name' if k.to_s.match(/[\x00-\x20\(\)<>@,;:\\\"\/\[\]\?\=\{\}\s]/.freeze); custom_headers << "Cookie: #{ k }=#{ Iodine::Http::Request.encode_url v }\r\n"} if @options[:cookies].is_a?(Hash)

				# send protocol upgrade request
				websocket_key = [(Array.new(16) {rand 255} .pack 'c*'.freeze )].pack('m0*'.freeze)
				(ssl || socket).write "GET #{url.path}#{url.query.to_s.empty? ? ''.freeze : ("?#{url.query}")} HTTP/1.1\r\nHost: #{url.host}#{url.port ? (":#{url.port.to_s}") : ''.freeze}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nOrigin: #{ssl ? 'https'.freeze : 'http'.freeze}://#{url.host}\r\nSec-WebSocket-Key: #{websocket_key}\r\nSec-WebSocket-Version: 13\r\n#{custom_headers}\r\n"
				# wait for answer - make sure we don't over-read
				# (a websocket message might be sent immidiately after connection is established)
				reply = String.new.force_encoding(::Encoding::ASCII_8BIT)
				stop_time = Time.now + (@options[:timeout] || 5)
				stop_reply = "\r\n\r\n".freeze
				until reply[-4..-1] == stop_reply
					begin
						reply << ( ssl ? ssl.read_nonblock(1) : socket.recv_nonblock(1) )
					rescue Errno::EWOULDBLOCK, OpenSSL::SSL::SSLErrorWaitReadable => e
						raise "Websocket client handshake timed out (HTTP reply not recieved)\n\n Got Only: #{reply}" if Time.now >= stop_time
						IO.select [socket], nil, nil, (@options[:timeout] || 5)
						retry
					end
					raise "Connection failed" if socket.closed?
				end
				# review reply
				raise "Connection Refused. Reply was:\r\n #{reply}" unless reply.lines[0].match(/^HTTP\/[\d\.]+ 101/i.freeze)
				raise 'Websocket Key Authentication failed.' unless reply.match(/^Sec-WebSocket-Accept:[\s]*([^\s]*)/i.freeze) && reply.match(/^Sec-WebSocket-Accept:[\s]*([^\s]*)/i.freeze)[1] == Digest::SHA1.base64digest(websocket_key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
				# read the body's data and parse any incoming data.
				@request = Iodine::Http::Request.new
				@request[:method] = 'GET'.freeze
				@request['host'.freeze] = "#{url.host}:#{url.port}"
				@request[:query] = url.path
				@request[:version] = '1.1'.freeze
				reply = StringIO.new reply
				reply.gets

				until reply.eof?
					until @request[:headers_complete] || (l = reply.gets).nil?
						if l.include? ':'
							l = l.strip.split(/:[\s]?/.freeze, 2)
							l[0].strip! ; l[0].downcase!;
							@request[l[0]] ? (@request[l[0]].is_a?(Array) ? (@request[l[0]] << l[1]) : @request[l[0]] = [@request[l[0]], l[1] ]) : (@request[l[0]] = l[1])
						elsif l =~ /^[\r]?\n/.freeze
							@request[:headers_complete] = true
						else
							#protocol error
							raise 'Protocol Error, closing connection.'
							return close
						end
					end
				end
				reply.string.clear

				return Iodine::Http::Websockets.new( ( ssl || socket), handler: self, request: @request )

			rescue => e
				(ssl || socket).tap {|io| next if io.nil?; io.close unless io.closed?}
				if @options[:on_error]
					@options[:on_error].call(e)
					return false
				end
				raise e unless @io
			end

			# Create a simple Websocket Client(!).
			#
			# This method accepts two parameters:
			# url:: a String representing the URL of the websocket. i.e.: 'ws://foo.bar.com:80/ws/path'
			# options:: a Hash with options to be used. The options will be used to define the connection's details (i.e. ssl etc') and the Websocket callbacks (i.e. on_open(ws), on_close(ws), on_message(ws))
			# &block:: an optional block that accepts one parameter (data) and will be used as the `#on_message(data)`
			#
			# Acceptable options are:
			# on_open:: the on_open callback - Must be an objects that answers `call()`, usually a Proc.
			# on_message:: the on_message callback - Must be an objects that answers `call(data)`, usually a Proc.
			# on_close:: the on_close callback - Must be an objects that answers `call()`, usually a Proc. The method is called when the connection is closed and isn't renewed.
			# on_error:: the on_error callback - Must be an objects that answers `call(err)`, usually a Proc. This is called whenever a connection fails to be established or an exception is raised by any of the callbacks. This is NOT the disconnection websocket message. dafaults to raising the error (error pass-through).
			# headers:: a Hash of custom HTTP headers to be sent with the request. Header data, including cookie headers, should be correctly encoded.
			# cookies:: a Hash of cookies to be sent with the request. cookie data will be encoded before being sent.
			# timeout:: the number of seconds to wait before the connection is established. Defaults to 5 seconds.
			# every:: this option, together with `:send` and `:renew`, implements a polling websocket. :every is the number of seconds between each polling event. without `:send`, this option will be ignored. defaults to nil.
			# send:: a String to be sent or a Proc to be performed each polling interval. This option, together with `:every` and `:renew`, implements a polling websocket. without `:every`, this option will be ignored. defaults to nil. If `:send` is a Proc, it will be executed within the context of the websocket client object, with acess to the websocket client's instance variables and methods.
			# renew:: the number of times to attempt to renew the connection if the connection is terminated by the remote server. Attempts are made in 2 seconds interval. The default for a polling websocket is 5 attempts to renew. For all other clients, the default is 0 (no renewal).
			#
			# The method will block until the connection is established or until 5 seconds have passed (the timeout). The method will either return a WebsocketClient instance object or raise an exception it the connection was unsuccessful.
			#
			# Use Iodine::Http.ws_connect for a non-blocking initialization.
			#
			# An #on_close callback will only be called if the connection isn't or cannot be renewed. If the connection is renewed,
			# the #on_open callback will be called again for a new Websocket client instance - but the #on_close callback will NOT be called.
			#
			# Due to this design, the #on_open and #on_close methods should NOT be used for opening IO resources (i.e. file handles) nor for cleanup IF the `:renew` option is enabled.
			#
			# An on_message Proc must be defined, or the method will fail.
			#
			# The on_message Proc can be defined using the optional block:
			#
			#      Iodine::Http::WebsocketClient.connect("ws://localhost:3000/") {|data| write data} #echo example
			#
			# OR, the on_message Proc can be defined using the options Hash: 
			#
			#      Iodine::Http::WebsocketClient.connect("ws://localhost:3000/", on_open: -> {}, on_message: -> {|data| write data })
			#
			# The #on_message(data), #on_open and #on_close methods will be executed within the context of the WebsocketClient
			# object, and will have native acess to the Websocket response object.
			#
			# After the WebsocketClient had been created, it's possible to update the #on_message and #on_close methods:
			#
			#      # updates #on_message
			#      wsclient.on_message do |data|
			#           response << "I'll disconnect on the next message!"
			#           # updates #on_message again.
			#           on_message {|data| disconnect }
			#      end
			#
			#
			# !!please be aware that the Websockt Client will not attempt to verify SSL certificates,
			# so that even SSL connections are vulnerable to a possible man in the middle attack.
			#
			# @return [Iodine::Http::WebsocketClient] this method returns the connected {Iodine::Http::WebsocketClient} or raises an exception if something went wrong (such as a connection timeout).
			def self.connect url, options={}, &block
				options = url if url.is_a?(Hash) && options.empty?
				options[:renew] ||= 5 if options[:every] && options[:send]
				options[:url] ||= url
				options[:on_message] ||= block
				client = self.new(options)
				return client unless client.closed?
				false
			end
		end
	end
end


