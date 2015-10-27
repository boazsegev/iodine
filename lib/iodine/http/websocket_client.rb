
module Iodine
	module Http
		# Create a simple Websocket Client(!).
		#
		# This should be done from within an Iodine task, or the callbacks will not be called.
		#
		# Use {Iodine::Http::WebsocketClient.connect} to initialize a client with all the callbacks needed.
		class WebsocketClient
		
			attr_accessor :response, :request, :params

			def initialize request
				@response = nil
				@request = request
				@params = request[:ws_client_params]
				@on_message = @params[:on_message]
				raise "Websocket client must have an #on_message Proc or handler." unless @on_message && @on_message.respond_to?(:call)
				@on_open = @params[:on_open]
				@on_close = @params[:on_close]
				@renew = @params[:renew].to_i
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
				instance_exec( data, &@on_message) 
			end

			def on_open
				raise 'The on_open even is invalid at this point.' if block_given?
				@io = @request[:io]
				Iodine::Http::Request.parse @request
				instance_exec(&@on_open) if @on_open
				if request[:ws_client_params][:every] && @params[:send]
					raise TypeError, "Websocket Client `:send` should be either a String or a Proc object." unless @params[:send].is_a?(String) || @params[:send].is_a?(Proc)
					Iodine.run_every @params[:every], self, @params do |ws, client_params, timer|
						if ws.closed?
							timer.stop!
							next
						end
						if client_params[:send].is_a?(String)
							ws.write client_params[:send]
						elsif client_params[:send].is_a?(Proc)
							ws.instance_exec(&client_params[:send])
						end
					end
				end
			end

			def on_close(&block)
				return @on_close = block if block
				if @renew > 0
					renew_proc = Proc.new do
						begin
							Iodine::Http::WebsocketClient.connect(@params[:url], @params)
						rescue
							@renew -= 1
							if @renew <= 0
								Iodine.fatal "WebsocketClient renewal FAILED for #{@params[:url]}"
								instance_exec(&@on_close) if @on_close
							else
								Iodine.run_after 2, &renew_proc
								Iodine.warn "WebsocketClient renewal failed for #{@params[:url]}, #{@renew} attempts left"
							end
							false
						end
					end
					renew_proc.call
				else
					instance_exec(&@on_close) if @on_close
				end
			end

			def on_shutdown
				@renew = 0
			end

			# closes the connection, if open
			def close
				@io.close if @io
			end

			# checks if the socket is open (if the websocket was terminated abnormally, this might returs true when it should be false).
			def closed?
				@io.io.closed? if @io && @io.io
			end

			# checks if this is an SSL websocket connection.
			def ssl?
				@request.ssl?
			end

			# return the HTTP's handshake data, including any cookies sent by the server.
			def request
				@request
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
				return false if @io.closed?
				data = data.dup # needed?
				unless op_code # apply extenetions to the message as a whole
					op_code = (data.encoding == ::Encoding::UTF_8 ? 1 : 2) 
					# @ws_extentions.each { |ex| ext |= ex.edit_message data } if @ws_extentions
				end
				byte_size = data.bytesize
				if byte_size > (::Iodine::Http::Websockets::FRAME_SIZE_LIMIT+2)
					sections = byte_size/FRAME_SIZE_LIMIT + (byte_size % ::Iodine::Http::Websockets::FRAME_SIZE_LIMIT ? 1 : 0)
					send_data( data.slice!( 0...::Iodine::Http::Websockets::FRAME_SIZE_LIMIT ), op_code, data.empty?, ext) && (ext = op_code = 0) until data.empty?
					return true # avoid sending an empty frame.
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
				@io.write header
				i = -1;
				@io.write(data.bytes.map! {|b| (b ^ mask[i = (i + 1)%4]) } .pack('C*'.freeze)) && true
			end
			alias :<< :write

			# Create a simple Websocket Client(!).
			#
			# This method accepts two parameters:
			# url:: a String representing the URL of the websocket. i.e.: 'ws://foo.bar.com:80/ws/path'
			# options:: a Hash with options to be used. The options will be used to define the connection's details (i.e. ssl etc') and the Websocket callbacks (i.e. on_open(ws), on_close(ws), on_message(ws))
			# &block:: an optional block that accepts one parameter (data) and will be used as the `#on_message(data)`
			#
			# Acceptable options are:
			# on_open:: the on_open callback. Must be an objects that answers `call(ws)`, usually a Proc.
			# on_message:: the on_message callback. Must be an objects that answers `call(ws)`, usually a Proc.
			# on_close:: the on_close callback - this will ONLY be called if the connection WASN'T renewed. Must be an objects that answers `call(ws)`, usually a Proc.
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
				@message ||= Iodine.warn("Deprecation Notice:\nIt is unlikely that Iodine 0.2.0 will support a blocking websocket client API.\nMake sure to use Iodine::Http.ws_connect and define an 'on_open' callback.") && true
				socket = nil
				options = options.dup
				options[:on_message] ||= block
				raise "No #on_message handler defined! please pass a block or define an #on_message handler!" unless options[:on_message]
				url = URI.parse(url) unless url.is_a?(URI)
				options[:url] = url
				options[:renew] ||= 5 if options[:every] && options[:send]

				ssl = url.scheme == "https" || url.scheme == "wss"

				url.port ||= ssl ? 443 : 80
				url.path = '/' if url.path.to_s.empty?
				socket = TCPSocket.new(url.host, url.port)
				if ssl
					context = OpenSSL::SSL::SSLContext.new
					context.cert_store = OpenSSL::X509::Store.new
					context.cert_store.set_default_paths
					context.set_params verify_mode: (options[:verify_mode] || OpenSSL::SSL::VERIFY_NONE) # OpenSSL::SSL::VERIFY_PEER #OpenSSL::SSL::VERIFY_NONE
					ssl = OpenSSL::SSL::SSLSocket.new(socket, context)
					ssl.sync_close = true
					ssl.connect
				end
				# prep custom headers
				custom_headers = ''
				custom_headers = options[:headers] if options[:headers].is_a?(String)
				options[:headers].each {|k, v| custom_headers << "#{k.to_s}: #{v.to_s}\r\n"} if options[:headers].is_a?(Hash)
				options[:cookies].each {|k, v| raise 'Illegal cookie name' if k.to_s.match(/[\x00-\x20\(\)<>@,;:\\\"\/\[\]\?\=\{\}\s]/); custom_headers << "Cookie: #{ k }=#{ Iodine::Http::Request.encode_url v }\r\n"} if options[:cookies].is_a?(Hash)

				# send protocol upgrade request
				websocket_key = [(Array.new(16) {rand 255} .pack 'c*' )].pack('m0*')
				(ssl || socket).write "GET #{url.path}#{url.query.to_s.empty? ? '' : ('?' + url.query)} HTTP/1.1\r\nHost: #{url.host}#{url.port ? (':'+url.port.to_s) : ''}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nOrigin: #{options[:ssl_client] ? 'https' : 'http'}://#{url.host}\r\nSec-WebSocket-Key: #{websocket_key}\r\nSec-WebSocket-Version: 13\r\n#{custom_headers}\r\n"
				# wait for answer - make sure we don't over-read
				# (a websocket message might be sent immidiately after connection is established)
				reply = ''
				reply.force_encoding(::Encoding::ASCII_8BIT)
				stop_time = Time.now + (options[:timeout] || 5)
				stop_reply = "\r\n\r\n"
				sleep 0.2
				until reply[-4..-1] == stop_reply
					begin
						reply << ( ssl ? ssl.read_nonblock(1) : socket.recv_nonblock(1) )
					rescue Errno::EWOULDBLOCK => e
						raise "Websocket client handshake timed out (HTTP reply not recieved)\n\n Got Only: #{reply}" if Time.now >= stop_time
						IO.select [socket], nil, nil, (options[:timeout] || 5)
						retry
					end
					raise "Connection failed" if socket.closed?
				end
				# review reply
				raise "Connection Refused. Reply was:\r\n #{reply}" unless reply.lines[0].match(/^HTTP\/[\d\.]+ 101/i)
				raise 'Websocket Key Authentication failed.' unless reply.match(/^Sec-WebSocket-Accept:[\s]*([^\s]*)/i) && reply.match(/^Sec-WebSocket-Accept:[\s]*([^\s]*)/i)[1] == Digest::SHA1.base64digest(websocket_key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
				# read the body's data and parse any incoming data.
				request = Iodine::Http::Request.new
				request[:method] = 'GET'
				request['host'] = "#{url.host}:#{url.port}"
				request[:query] = url.path
				request[:version] = '1.1'
				reply = StringIO.new reply
				reply.gets

				until reply.eof?
					until request[:headers_complete] || (l = reply.gets).nil?
						if l.include? ':'
							l = l.strip.split(/:[\s]?/, 2)
							l[0].strip! ; l[0].downcase!;
							request[l[0]] ? (request[l[0]].is_a?(Array) ? (request[l[0]] << l[1]) : request[l[0]] = [request[l[0]], l[1] ]) : (request[l[0]] = l[1])
						elsif l =~ /^[\r]?\n/
							request[:headers_complete] = true
						else
							#protocol error
							raise 'Protocol Error, closing connection.'
							return close
						end
					end
				end
				reply.string.clear

				request[:ws_client_params] = options
				client = self.new(request)
				Iodine::Http::Websockets.new( ( ssl || socket), handler: client, request: request )

				return client	

				rescue => e
					(ssl || socket).tap {|io| next if io.nil?; io.close unless io.closed?}
					raise e
			end
		end
	end
end


