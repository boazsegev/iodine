module Iodine
	module Http
		class Websockets < ::Iodine::Protocol
			# continue to initialize the websocket protocol.
			def on_open
				@handler = @options[:handler]
				@ws_extentions = @options[:ext]
				@options[:request][:io] = self
				@parser = {body: String.new, stage: 0, step: 0, mask_key: [], len_bytes: []}
				set_timeout self.class.default_timeout
				@handler.on_open if @handler.respond_to? :on_open
			end
			# parse and handle messages.
			def on_message data
				extract_message StringIO.new(data)
			end
			# handle broadcasts.
			def on_broadcast data
				@locker.synchronize { @handler.on_broadcast(data) } if @handler.respond_to? :on_broadcast
			end
			# cleanup after closing.
			def on_close
				@handler.on_close if @handler.respond_to? :on_close
				if @ws_extentions
					@ws_extentions.each { |ex| ex.close }
					@ws_extentions.clear
				end
			end

			# a politer disconnection.
			def go_away
				write CLOSE_FRAME
				close
			end

			# a politer disconnection during shutdown.
			def on_shutdown
				@handler.on_shutdown if @handler.respond_to?(:on_shutdown)
				go_away
			end

			# allow Http responses to be used for sending Websocket data.
			def send_response response, finish = false
				body = response.extract_body
				send_data body.read
			end
			alias :stream_response :send_response

			# Sends the data as one (or more) Websocket frames.
			#
			# Use THIS method to send data using the Websocket protocol.
			# Using {Iodine::Protocol#write} will bypass the Websocket data framing and send the raw data, breaking the connection.
			def send_data data, op_code = nil, fin = true, ext = 0
				return false if !data || data.empty?
				return false if @io.closed?
				data = data.dup # needed?
				unless op_code # apply extenetions to the message as a whole
					op_code = (data.encoding == ::Encoding::UTF_8 ? 1 : 2) 
					@ws_extentions.each { |ex| ext |= ex.edit_message data } if @ws_extentions
				end
				byte_size = data.bytesize
				if byte_size > (FRAME_SIZE_LIMIT+2)
					# sections = byte_size/FRAME_SIZE_LIMIT + (byte_size%FRAME_SIZE_LIMIT ? 1 : 0)
					send_data( data.slice!( 0...FRAME_SIZE_LIMIT ), op_code, data.empty?, ext) && (ext = op_code = 0) until data.empty?
					return true # avoid sending an empty frame.
				end
				@ws_extentions.each { |ex| ext |= ex.edit_frame data } if @ws_extentions
				header = ( (fin ? 0b10000000 : 0) | (op_code & 0b00001111) | ext).chr.force_encoding(::Encoding::ASCII_8BIT)

				if byte_size < 125
					header << byte_size.chr
				elsif byte_size.bit_length <= 16					
					header << 126.chr
					header << [byte_size].pack('S>'.freeze)
				else
					header << 127.chr
					header << [byte_size].pack('Q>'.freeze)
				end
				write header
				write(data) && true
			end
			alias :<< :send_data

			# Sends a ping and calles the :on_ping callback (if exists).
			def ping
				write(PING_FRAME) && ( (@handler.respond_to?(:on_ping) && @handler.on_ping) || true)
			end
			# Sends an empty pong.
			def pong
				write PONG_FRAME
			end

			# Broadcasts data to ALL the websocket connections EXCEPT the once specified (if specified).
			#
			# Data broadcasted will be recived by the websocket handler it's #on_broadcast(ws) method (if exists).
			#
			# Accepts:
			#
			# data:: One object of data. Usually a Hash, Array, String or a JSON formatted object.
			# ignore_io (optional):: The IO to be ignored by the broadcast. Usually the broadcaster's IO.
			# 
			def self.broadcast data, ignore_io = nil
				if ignore_io
					ig_id = ignore_io.object_id
					each {|io| Iodine.run io, data, &broadcast_proc unless io.object_id == ig_id}
				else
					each {|io| Iodine.run io, data, &broadcast_proc }
				end
				true
			end

			# Broadcasts the data to all the listening websockets, except self. See {::Iodine::Http::Websockets.broadcast}
			def broadcast data
				self.class.broadcast data, self
			end

			# Unicast data to a specific websocket connection (ONLY the connection specified).
			#
			# Data broadcasted will be recived by the websocket handler it's #on_broadcast(ws) method (if exists).
			# Accepts:
			# uuid:: the UUID of the websocket connection recipient.
			# data:: the data to be sent.
			#
			# @return [true, false] Returns true if the object was found and the unicast was sent (the task will be executed asynchronously once the unicast was sent).
			def self.unicast id, data
				return false unless id && data
				each {|io| next unless io.id == id; Iodine.run io, data, &broadcast_proc; return true}
				false
			end
			# @return [true, false] Unicasts the data to the requested connection. returns `true` if the requested connection id was found on this server. See {::Iodine::Http::Websockets.unicast}
			def unicast id, data
				self.class.unicast id, data
			end

			def self.handshake request, response, handler
				# review handshake (version, extentions)
				# should consider adopting the websocket gem for handshake and framing:
				# https://github.com/imanel/websocket-ruby
				# http://www.rubydoc.info/github/imanel/websocket-ruby
				return refuse response unless handler || handler == true
				io = request[:io]
				response.keep_alive = true
				response.status = 101
				response['upgrade'.freeze] = 'websocket'.freeze
				response['content-length'.freeze] = '0'.freeze
				response['connection'.freeze] = 'Upgrade'.freeze
				response['sec-websocket-version'.freeze] = '13'.freeze
				# Note that the client is only offering to use any advertised extensions
				# and MUST NOT use them unless the server indicates that it wishes to use the extension.
				ws_extentions = []
				ext = []
				request['sec-websocket-extensions'.freeze].to_s.split(/[\s]*[,][\s]*/.freeze).each {|ex| ex = ex.split(/[\s]*;[\s]*/.freeze); ( ( tmp = SUPPORTED_EXTENTIONS[ ex[0] ].call(ex[1..-1]) ) && (ws_extentions << tmp) && (ext << tmp.name) ) if SUPPORTED_EXTENTIONS[ ex[0] ] }
				ext.compact!
				if ext.any?
					response['sec-websocket-extensions'.freeze] = ext.join(', '.freeze)
				else
					ws_extentions = nil
				end
				response['sec-websocket-accept'.freeze] = Digest::SHA1.base64digest(request['sec-websocket-key'.freeze] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'.freeze)
				response.session
				# Iodine.log "#{@request[:client_ip]} [#{Time.now.utc}] - #{@connection.object_id} Upgraded HTTP to WebSockets.\n"
				response.finish
				self.new(io.io, handler: handler, request: request, ext: ws_extentions)
				return true
			end

			# Gets the new connection timeout in seconds. Whenever this timeout is reached, a ping will be sent. Defaults to 40 (seconds).
			def self.default_timeout
				@default_timeout
			end
			# Sets the new connection timeout in seconds. Whenever this timeout is reached, a ping will be sent. Defaults to 40 (seconds).
			def self.default_timeout= val
				@default_timeout = val
			end
			# Sets the message byte size limit for a Websocket message. Defaults to 0 (no limit)
			#
			# Although memory will be allocated for the latest TCP/IP frame,
			# this allows the websocket to disconnect if the incoming expected message size exceeds the allowed maximum size.
			#
			# If the message size limit is exceeded, the disconnection will be immidiate as an attack will be assumed. The protocol's normal disconnect sequesnce will be discarded.
			def self.message_size_limit=val
				@message_size_limit = val
			end
			# Gets the message byte size limit for a Websocket message. Defaults to 0 (no limit)
			def self.message_size_limit
				@message_size_limit ||= 0
			end

			protected
			FRAME_SIZE_LIMIT = 17_895_697
			SUPPORTED_EXTENTIONS = {}
			CLOSE_FRAME = "\x88\x00".freeze
			PONG_FRAME = "\x8A\x00".freeze
			PING_FRAME = "\x89\x00".freeze
			@default_timeout = 40

			def self.broadcast_proc
				@broadcast_proc ||= Proc.new {|io, data| io.on_broadcast data }
			end

			def self.refuse response
				response.status = 400
				response['sec-websocket-extensions'.freeze] = SUPPORTED_EXTENTIONS.keys.join(', '.freeze)
				response['sec-websocket-version'.freeze] = '13'.freeze
				false
			end

			# parse the message and send it to the handler
			#
			# test: frame = ["819249fcd3810b93b2fb69afb6e62c8af3e83adc94ee2ddd"].pack("H*").bytes; parser[:stage] = 0; parser = {}
			# accepts:
			# data:: an IO object (usually a StringIO object)
			def extract_message data
				parser = @parser
				until data.eof?
					if parser[:stage] == 0
						tmp = data.getbyte
						return unless tmp
						parser[:fin] = tmp[7] == 1
						parser[:rsv1] = tmp[6] == 1
						parser[:rsv2] = tmp[5] == 1
						parser[:rsv3] = tmp[4] == 1
						parser[:op_code] = tmp & 0b00001111
						parser[:p_op_code] ||= tmp & 0b00001111
						parser[:stage] += 1
					end
					if parser[:stage] == 1
						tmp = data.getbyte
						return unless tmp
						parser[:mask] = tmp[7]
						parser[:mask_key].clear
						parser[:len] = tmp & 0b01111111
						parser[:len_bytes].clear
						parser[:stage] += 1
					end
					if parser[:stage] == 2
						tmp = 0
						tmp = 2 if parser[:len] == 126
						tmp = 8 if parser[:len] == 127
						while parser[:len_bytes].length < tmp
							parser[:len_bytes] << data.getbyte
							return parser[:len_bytes].pop unless parser[:len_bytes].last
						end
						parser[:len] = merge_bytes( parser[:len_bytes] ) if tmp > 0
						parser[:step] = 0
						parser[:stage] += 1
						return false unless review_message_size
					end
					if parser[:stage] == 3 && parser[:mask] == 1
						until parser[:mask_key].length == 4
							parser[:mask_key] << data.getbyte
							return parser[:mask_key].pop unless parser[:mask_key].last
						end
						parser[:stage] += 1
					elsif  parser[:stage] == 3 && parser[:mask] != 1
						parser[:stage] += 1
					end
					if parser[:stage] == 4
						if parser[:body].bytesize < parser[:len]
							tmp = data.read(parser[:len] - parser[:body].bytesize)
							return unless tmp
							parser[:body] << tmp
						end
						if parser[:body].bytesize >= parser[:len]
							tmp = -1
							parser[:body] = parser[:body].bytes.map! {|b| (b ^ parser[:mask_key][tmp = (tmp + 1)%4]) } .pack('C*'.freeze) if parser[:mask] == 1
							# parser[:body].bytesize.times {|i| parser[:body][i] = (parser[:body][i].ord ^ parser[:mask_key][i % 4]).chr} if parser[:mask] == 1
							parser[:stage] = 99
						end
					end
					complete_frame if parser[:stage] == 99
				end
			end

			# takes and Array of bytes and combines them to an int(16 Bit), 32Bit or 64Bit number
			def merge_bytes bytes
				return 0 unless bytes.any?
				return bytes.pop if bytes.length == 1
				bytes.pop ^ (merge_bytes(bytes) << 8)
			end

			# handles the completed frame and sends a message to the handler once all the data has arrived.
			def complete_frame
				parser = @parser
				@ws_extentions.each {|ex| ex.parse_frame(parser) } if @ws_extentions

				case parser[:op_code]
				when 9 # ping
					# handle parser[:op_code] == 9 (ping)
					::Iodine.run { send_data parser[:body], 10 }
					parser[:p_op_code] = nil if parser[:p_op_code] == 9
				when 10 #pong
					# handle parser[:op_code] == 10 (pong)
					parser[:p_op_code] = nil if parser[:p_op_code] == 10
				when 8 # close
					# handle parser[:op_code] == 8 (close)
					write( CLOSE_FRAME )
					close
					parser[:p_op_code] = nil if parser[:p_op_code] == 8
				else
					parser[:message] ? ((parser[:message] << parser[:body]) && parser[:body].clear) : ((parser[:message] = parser[:body]) && parser[:body] = String.new)
					# handle parser[:op_code] == 0 / fin == false (continue a frame that hasn't ended yet)
					if parser[:fin]
						@ws_extentions.each {|ex| ex.parse_message(parser) } if @ws_extentions
						Iodine::Http::Request.make_utf8! parser[:message] if parser[:p_op_code] == 1
						@handler.on_message parser[:message]
						parser[:message] = nil
						parser[:p_op_code] = nil
					end
				end
				parser[:stage] = 0
				parser[:body].clear
				parser[:step] = 0
				parser[:mask_key].clear
				parser[:p_op_code] = nil
			end
			#reviews the message size and closes the connection if expected message size is over the allowed limit.
			def review_message_size
				if ( self.class.message_size_limit.to_i > 0 ) && ( ( @parser[:len] + (@parser[:message] ? @parser[:message].bytesize : 0) ) > self.class.message_size_limit.to_i )
					close
					@parser.delete :message
					@parser[:step] = 0
					@parser[:body].clear
					@parser[:mask_key].clear
					Iodine.warn "Websocket message above limit's set - closing connection."
					return false
				end
				true
			end


		end
	end
end


