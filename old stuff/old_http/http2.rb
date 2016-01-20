module Iodine
	module Http
		class Http2 < ::Iodine::Protocol
			def on_open
				# not fully fanctional.
				::Iodine.warn "Http/2 requested - support is still experimental."

				# update the timeout to 15 seconds (ping will be sent whenever timeout is reached).
				set_timeout 15

				# Header compression is stateful
				@hpack = ::Iodine::Http::Http2::HPACK.new

				# the header-stream cache
				@header_buffer = String.new
				@header_end_stream = false
				@header_sid = nil
				@frame_locker = Mutex.new

				# frame parser starting posotion
				@frame = {}

				# Open stream managment
				@open_streams = {}

				# connection is only established after the preface was sent
				@connected = false

				# the last stream to be processed (For the GOAWAY frame)
				@last_stream = 0
				@refuese_new = false
				# @complete_stream = 0

				# the settings state
				@settings = DEFAULT_SETTING.dup

				# send connection preface (Section 3.5) consisting of a (can be empty) SETTINGS frame (Section 6.5).
				#
				# should prepare to accept a client connection preface which starts with:
				# 0x505249202a20485454502f322e300d0a0d0a534d0d0a0d0a
				# == PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n
				# + SETTINGS frame

				# The @options variable contains the original Http1 request, if exists.
				return unless @options
				::Iodine.warn "Http/2: upgrade handshake settings not implemented. upgrade request:\n#{@options}"
				@last_stream = @options[:stream_id] = 1
				@options[:io] = self
				# deal with the request['http2-settings'] - NO ACK
				# HTTP2-Settings: <base64url encoding of HTTP/2 SETTINGS payload>

				# dispatch the original request
				::Iodine.run @options, &(::Iodine::Http::Http2.dispatch)
				@options = nil
			end
			def on_message data
				data = ::StringIO.new data
				parse_preface data unless @connected
				true while parse_frame data
			end

			def send_response response
				return false if response.headers.frozen?
				body = response.extract_body
				request = response.request
				return body && body.close unless send_headers response, request
				return log_finished(response) && body && body.close if request.head?
				if body
					until body.eof?
						response.bytes_written += emit_payload(body.read(@settings[SETTINGS_MAX_FRAME_SIZE], Thread.current[:write_buffer]), request[:sid], 0, (body.eof? ? 1 : 0))
					end
					body.close
				else
					emit_payload(''.freeze, request[:sid], 0, 1)
				end
				log_finished response
			end

			def stream_response response, finish = false
				request = response.request
				body = response.extract_body
				send_headers response, request
				return body && body.close if request.head?
				if body
					response.bytes_written += emit_payload body, request[:sid], 0,(finish ? 1 : 0)
					body.close
				elsif finish
					emit_payload(''.freeze, request[:sid], 0, 1)
				end
				log_finished response if finish
			end

			def ping
				@frame_locker.synchronize { emit_frame "pniodine".freeze, 0, 6 }
			end

			def push request
				return false if @settings[SETTINGS_ENABLE_PUSH] == 0
				@last_push ||= 0
				# emit push promise
				emit_payload @hpack.encode(path: request[:path], method: request[:method], scheme: request[:scheme], authority: request[:authority]), (request[:sid] = (@last_push += 2)), 5, 4
				# queue for app dispatch
				Iodine.run( request, &::Iodine::Http::Http2.dispatch)
			end

			def go_away error_code
				return false if @io.closed?
				@frame_locker.synchronize { emit_frame [@last_stream, error_code].pack('N*'), 0, 7 }
				close
				# Iodine.info "HTTP/2 connection closed with code #{error_code}"
			end

			# Gracefully close HTTP/2 when possible
			def on_shutdown
				go_away NO_ERROR
			end

			# clear text handshake
			def self.handshake request, io, data
				return false unless request['upgrade'.freeze] =~ /h2c/.freeze && request['http2-settings'.freeze]
				io.write "HTTP/1.1 101 Switching Protocols\r\nConnection: Upgrade\r\nUpgrade: h2c\r\n\r\n".freeze
				http_2 = self.new(io, request)
				unless data.eof?
					http_2.on_message data.read
				end
			end
			# preknowledge handshake
			def self.pre_handshake io, data
				return false unless data[0..23] == "PRI * HTTP\/2.0\r\n\r\nSM\r\n\r\n".freeze
				self.new(io).on_message data
				true
			end

			
			protected

			# logs the sent response.
			def log_finished response
				request = response.request
				return if Iodine.logger.nil? || request[:no_log]
				t_n = Time.now
				# (Thread.current[:log_buffer] ||= String.new).clear
				# Thread.current[:log_buffer] << "#{request[:client_ip]} [#{t_n.utc}] #{request[:method]} #{request[:original_path]} #{request[:scheme]}\/2 #{response.status} #{response.bytes_written.to_s} #{((t_n - request[:time_recieved])*1000).round(2)}ms\n"
				# Iodine.log Thread.current[:log_buffer]
				Iodine.log("#{request[:client_ip]} [#{t_n.utc}] #{request[:method]} #{request[:original_path]} #{request[:scheme]}\/2 #{response.status} #{response.bytes_written.to_s} #{((t_n - request[:time_recieved])*1000).round(2)}ms\n").clear
			end

			def send_headers response, request
				return false if response.headers.frozen?
				headers = response.headers
				# headers[:status] = response.status.to_s
				headers['set-cookie'.freeze] = response.extract_cookies
				headers.freeze
				(Thread.current[:headers_buffer] ||= String.new).clear
				Thread.current[:headers_buffer] <<  @hpack.encode(status: response.status)
				Thread.current[:headers_buffer] <<  @hpack.encode(headers)
				emit_payload Thread.current[:headers_buffer], request[:sid], 1, (request.head? ? 1 : 0)
				return true
			end

			# Sends an HTTP frame with the requested payload
			#
			# @return [true, false] returns true if the frame was sent and false if the frame couldn't be sent (i.e. payload too big, connection closed etc').
			def emit_frame payload, sid = 0, type = 0, flags = 0
				# puts "Sent: #{[payload.bytesize, type, flags, sid, payload].pack('N C C N a*'.freeze)[1..-1].inspect}"
				@io.write( [payload.bytesize, type, flags, sid, payload].pack('N C C N a*'.freeze)[1..-1] ) #.tap {|s| next if type !=1 ;puts "Frame: #{s.class.name} - #{s.encoding}"; puts s.inspect } )
			end

			# Sends an HTTP frame group with the requested payload. This means the group will not be interrupted and will be sent as one unit.
			#
			# @return [true, false] returns true if the frame was sent and false if the frame couldn't be sent (i.e. payload too big, connection closed etc').
			def emit_payload payload, sid = 0, type = 0, flags = 0
				max_frame_size = @settings[SETTINGS_MAX_FRAME_SIZE]
				max_frame_size = 131_072 if max_frame_size > 131_072
				return @frame_locker.synchronize { emit_frame(payload, sid, type, ( (type == 0x1 || type == 0x5) ? (flags | 0x4) : flags ) ) } if payload.bytesize <= max_frame_size
				sent = 0
				payload = StringIO.new payload unless payload.respond_to? :read
				if type == 0x1 || type == 0x5
					@frame_locker.synchronize do
						sent += emit_frame(payload.read(max_frame_size, Thread.current[:write_buffer]), sid, 0x1, flags & 254)
						sent += emit_frame(payload.read(max_frame_size, Thread.current[:write_buffer]), sid, 0x9, 0) while payload.size - payload.pos > max_frame_size
						sent += emit_frame(payload.read(max_frame_size, Thread.current[:write_buffer]), sid, 0x9, (0x4 | (flags & 0x1)) )
					end
					return sent
				end
				sent += @frame_locker.synchronize { emit_frame(payload.read(max_frame_size, Thread.current[:write_buffer]), sid, type, (flags & 254)) } while payload.size - payload.pos > max_frame_size
				sent += @frame_locker.synchronize { emit_frame(payload.read(max_frame_size, Thread.current[:write_buffer]), sid, type, flags) }
			end

			def parse_preface data
				unless data.read(24) == "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n".freeze
					data.string.clear
					data.rewind
					return (connection_error(PROTOCOL_ERROR) && Iodine.warn("Preface not given"))
				end
				@connected = true
				emit_frame ''.freeze, 0, 0x4
				true
			end

			def parse_frame data
				frame = (@frame ||= {})
				unless frame[:length]
					tmp = (frame[:length_bytes] ||= "\x00")
					tmp << data.read(4 - tmp.bytesize).to_s
					return false if tmp.bytesize < 4
					frame[:length] = frame.delete(:length_bytes).unpack('N*'.freeze).pop
				end
				# TODO: error if length is greater than max_size (16_384 is default)
				if frame[:length] > @settings[SETTINGS_MAX_FRAME_SIZE]
					return false unless connection_error FRAME_SIZE_ERROR
				end
				unless frame[:type]
					tmp = data.getc
					return false unless tmp
					frame[:type] = tmp.ord
				end
				unless frame[:flags]
					tmp = data.getc
					return false unless tmp
					frame[:flags] = tmp.ord
				end
				unless frame[:sid]
					tmp = (frame[:sid_bytes] ||= String.new)
					tmp << data.read(4 - tmp.bytesize).to_s
					return false if tmp.bytesize < 4
					tmp = frame.delete(:sid_bytes).unpack('N')[0]
					frame[:sid] = tmp & 2147483647
					frame[:R] = tmp & 2147483648
				end
				tmp = (frame[:body] ||= String.new)
				tmp << data.read(frame[:length] - tmp.bytesize).to_s
				return false if tmp.bytesize < frame[:length]
				#TODO: something - Async?
				process_frame frame 
				# reset frame buffer
				@frame = {} # @frame.clear
				true
			end

			def process_frame frame
				# puts "processing HTTP/2 frame: #{frame}"
				(frame[:stream] = ( @open_streams[frame[:sid]] ||= ::Iodine::Http::Request.new(self) ) ) && (frame[:stream][:sid] ||= frame[:sid]) if frame[:sid] != 0
				case frame[:type]
				when 0 # DATA
					process_data frame
				when 1, 9 # HEADERS, CONTINUATION
					process_headers frame
				when 2 # PRIORITY
				when 3 # RST_STREAM
					@open_streams.delete frame[:sid]
				when 4 # SETTINGS
					process_settings frame
				# when 5 # PUSH_PROMISE - Should only be sent by the server
				when 6 # PING
					process_ping frame
				when 7 # GOAWAY
					go_away NO_ERROR
					Iodine.error "Http2 Disconnection with error (#{frame[:flags].to_s}): #{frame[:body].strip}" unless frame[:flags] == 0 && frame[:body] == ''.freeze
				when 8 # WINDOW_UPDATE
				else # Error, frame not recognized
				end

				# The PING frame (type=0x6) (most important!!!) is a mechanism for measuring a minimal round-trip time from the sender, as well as determining whether an idle connection is still functional
				#   ACK flag: 0x1 - if not present, must send frame back.
				#   PING frames are not associated with any individual stream. If a PING frame is received with a stream identifier field value other than 0x0, the recipient MUST respond with a connection error (Section 5.4.1) of type PROTOCOL_ERROR.
				# DATA frames (type=0x0) convey arbitrary, variable-length sequences of octets associated with a stream. One or more DATA frames are used, for instance, to carry HTTP request or response payloads
				# The HEADERS frame (type=0x1) 
				# The RST_STREAM frame (type=0x3 - 32bit error code) allows for immediate termination of a stream. RST_STREAM is sent to request cancellation of a stream or to indicate that an error condition has occurred.
				# The SETTINGS frame (type=0x4) conveys configuration parameters that affect how endpoints communicate.
				#     The payload of a SETTINGS frame consists of zero or more parameters, each consisting of an unsigned 16-bit setting identifier and an unsigned 32-bit value
				# The CONTINUATION frame (type=0x9)
				#	The CONTINUATION frame defines the following flag:
				#	END_HEADERS (0x4):
				#	When set, bit 2 indicates that this frame ends a header block
				# The PRIORITY frame (type=0x2) specifies the sender-advised priority of a stream (Section 5.3). It can be sent in any stream state, including idle or closed streams
				# The PUSH_PROMISE frame (type=0x5) is used to notify the peer endpoint in advance of streams the sender intends to initiate.
				# The GOAWAY frame (type=0x7) is used to initiate shutdown of a connection or to signal serious error conditions.
				#   The GOAWAY frame applies to the connection, not a specific stream (DIS 0x0)
				#   R (1 bit) LAST_STREAM_ID  (31 bit) ERROR_CODE (32 bit) DEBUG_DATA(optional) (*)
				# The WINDOW_UPDATE frame (type=0x8) is used to implement flow control
				#   A WINDOW_UPDATE frame with a length other than 4 octets MUST be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.

			end

			def process_ping frame
				# Iodine.info "Got HTTP/2 #{frame[:flags][0] == 1 ? 'Pong' : 'Ping'}"
				return connection_error PROTOCOL_ERROR if frame[:sid].to_i > 0
				return true if frame[:flags][0] == 1
				emit_frame frame[:body], 0, 6, 1
				# Iodine.info "Sent HTTP/2 'Pong'"
			end
			def process_headers frame
				if @header_sid && (frame[:type] == 1 || frame[:sid] != @header_sid)
					return connection_error PROTOCOL_ERROR
				end
				@header_end_stream = true if frame[:type] == 1 && frame[:flags][0] == 1

				if frame[:flags][3] == 1 # padded
					frame[:body] = frame[:body][1...(0 - frame[:body][0].ord)]
				end
				if frame[:flags][5] == 1 # priority
					# stream_dependency = frame[:body][0..3]
					# weight = frame[:body][4]
					frame[:body] = frame[:body][5..-1]
				end

				@header_buffer << frame[:body]

				frame[:stream][:headers_size] ||= 0
				frame[:stream][:headers_size] += frame[:body].bytesize

				return (Iodine.warn('Http2 header overloading, closing connection.') && connection_error( ENHANCE_YOUR_CALM ) ) if frame[:stream][:headers_size] > 262_144

				return unless frame[:flags][2] == 1 # fin

				frame[:stream].update @hpack.decode(@header_buffer) # this is where HPACK comes in
				return (Iodine.warn('Http2 header overloading, closing connection.') && connection_error( ENHANCE_YOUR_CALM ) ) if frame[:stream].length > 2096
				frame[:stream][:time_recieved] ||= Iodine.time
				frame[:stream][:version] ||= '2'.freeze

				process_request(@open_streams.delete frame[:sid]) if @header_end_stream

				# TODO: manage headers and streams

				@header_buffer.clear
				@header_end_stream = false
				@header_sid = nil
			rescue => e
				connection_error 5
				Iodine.warn e
			end
			def process_data frame
				if frame[:flags][3] == 1 # padded
					frame[:body] = frame[:body][1...(0 - frame[:body][0].ord)]
				end

				(frame[:stream][:body] ||= Tempfile.new('iodine'.freeze, :encoding => 'binary'.freeze) ) << frame[:body]

				# check request size
				if frame[:stream][:body].size > ::Iodine::Http.max_body_size
					Iodine.warn("Http2 payload (message size) too big (Iodine::Http.max_body_size == #{::Iodine::Http.max_body_size} bytes) - #{frame[:stream][:body].size} bytes.")
					return connection_error( ENHANCE_YOUR_CALM )
				end

				process_request(@open_streams.delete frame[:sid]) if frame[:flags][0] == 1
			end

			# # Settings Codes:
			SETTINGS_HEADER_TABLE_SIZE = 0x1
			# Allows the sender to inform the remote endpoint of the maximum size of the header compression table used to decode header blocks, in octets.
			# The encoder can select any size equal to or less than this value by using signaling specific to the header compression format inside a header block (see [COMPRESSION]).
			# The initial value is 4,096 octets.
			SETTINGS_ENABLE_PUSH = 0x2
			# This setting can be used to disable server push (Section 8.2). An endpoint MUST NOT send a PUSH_PROMISE frame if it receives this parameter set to a value of 0. An endpoint that has both set this parameter to 0 and had it acknowledged MUST treat the receipt of a PUSH_PROMISE frame as a connection error (Section 5.4.1) of type PROTOCOL_ERROR.
			# The initial value is 1, which indicates that server push is permitted. Any value other than 0 or 1 MUST be treated as a connection error (Section 5.4.1) of type PROTOCOL_ERROR.
			SETTINGS_MAX_CONCURRENT_STREAMS = 0x3
			# Indicates the maximum number of concurrent streams that the sender will allow. This limit is directional: it applies to the number of streams that the sender permits the receiver to create.
			# Initially, there is no limit to this value. It is recommended that this value be no smaller than 100, so as to not unnecessarily limit parallelism.
			# A value of 0 for SETTINGS_MAX_CONCURRENT_STREAMS SHOULD NOT be treated as special by endpoints.
			# A zero value does prevent the creation of new streams; however, this can also happen for any limit that is exhausted with active streams.
			# Servers SHOULD only set a zero value for short durations; if a server does not wish to accept requests, closing the connection is more appropriate.
			SETTINGS_INITIAL_WINDOW_SIZE = 0x4
			# Indicates the sender's initial window size (in octets) for stream-level flow control. The initial value is 216-1 (65,535) octets.
			# This setting affects the window size of all streams (see Section 6.9.2).
			# Values above the maximum flow-control window size of 231-1 MUST be treated as a connection error (Section 5.4.1) of type FLOW_CONTROL_ERROR.
			SETTINGS_MAX_FRAME_SIZE = 0x5
			# Indicates the size of the largest frame payload that the sender is willing to receive, in octets.
			# The initial value is 214 (16,384) octets. The value advertised by an endpoint MUST be between this initial value and
			# the maximum allowed frame size (224-1 or 16,777,215 octets), inclusive. Values outside this range MUST be treated as a connection error (Section 5.4.1) of type PROTOCOL_ERROR.
			SETTINGS_MAX_HEADER_LIST_SIZE = 0x6
			# This advisory setting informs a peer of the maximum size of header list that the sender is prepared to accept, in octets. The value is based on the uncompressed size of header fields, including the length of the name and value in octets plus an overhead of 32 octets for each header field.
			DEFAULT_SETTING = { SETTINGS_ENABLE_PUSH => 1,
				SETTINGS_INITIAL_WINDOW_SIZE => 65_535,
				SETTINGS_MAX_FRAME_SIZE => 16_384
			}

			def process_settings frame
				return if frame[:flags] == 1 # do nothing if it's only an ACK.
				return connection_error PROTOCOL_ERROR unless frame[:sid] == 0 && (frame[:body].bytesize % 6) == 0
				settings = StringIO.new frame[:body]
				until settings.eof?
					key = settings.read(2).unpack('n'.freeze)[0]
					value = settings.read(4).unpack('N'.freeze)[0]
					Iodine.info "HTTP/2 set #{key}=>#{value} for SID #{frame[:sid]}"
					case frame[:body][0..1].unpack('n'.freeze)[0]
					when SETTINGS_HEADER_TABLE_SIZE
						return connection_error ENHANCE_YOUR_CALM if value > 4096
						@hpack.resize(value)
					when SETTINGS_ENABLE_PUSH
						@settings[SETTINGS_ENABLE_PUSH] = value
					when SETTINGS_MAX_CONCURRENT_STREAMS
						@settings[SETTINGS_MAX_CONCURRENT_STREAMS] = value
					when SETTINGS_INITIAL_WINDOW_SIZE
						@settings[SETTINGS_INITIAL_WINDOW_SIZE] = value
					when SETTINGS_MAX_FRAME_SIZE
						@settings[SETTINGS_MAX_FRAME_SIZE] = value
					when SETTINGS_MAX_HEADER_LIST_SIZE
						@settings[SETTINGS_MAX_HEADER_LIST_SIZE] = value
					else
						# Unsupported parameters MUST be ignored
					end
				end
				emit_frame ''.freeze, 0, 4, 1
			end

			def process_request request
				return if @refuese_new
				::Iodine::Http::Request.parse request
				# Iodine.info "Should Process request #{request.select { |k,v| k != :io } }"
				@last_stream = request[:sid] if request[:sid] > @last_stream
				# emit_frame [HTTP_1_1_REQUIRED].pack('N'), request[:sid], 0x3, 0
				::Iodine.run request, &(::Iodine::Http::Http2.dispatch)

			end

			# Error codes:

			# The associated condition is not a result of an error. For example, a GOAWAY might include this code to indicate graceful shutdown of a connection.
			NO_ERROR = 0x0
			# The endpoint detected an unspecific protocol error. This error is for use when a more specific error code is not available.
			PROTOCOL_ERROR = 0x1
			# The endpoint encountered an unexpected internal error.
			INTERNAL_ERROR = 0x2
			# The endpoint detected that its peer violated the flow-control protocol.
			FLOW_CONTROL_ERROR = 0x3
			# The endpoint sent a SETTINGS frame but did not receive a response in a timely manner. See Section 6.5.3 ("Settings Synchronization").
			SETTINGS_TIMEOUT = 0x4
			# The endpoint received a frame after a stream was half-closed.
			STREAM_CLOSED = 0x5
			# The endpoint received a frame with an invalid size.
			FRAME_SIZE_ERROR = 0x6
			# The endpoint refused the stream prior to performing any application processing (see Section 8.1.4 for details).
			REFUSED_STREAM = 0x7
			# Used by the endpoint to indicate that the stream is no longer needed.
			CANCEL = 0x8
			# The endpoint is unable to maintain the header compression context for the connection.
			COMPRESSION_ERROR = 0x9
			# The connection established in response to a CONNECT request (Section 8.3) was reset or abnormally closed.
			CONNECT_ERROR = 0xa
			# The endpoint detected that its peer is exhibiting a behavior that might be generating excessive load.
			ENHANCE_YOUR_CALM = 0xb
			# The underlying transport has properties that do not meet minimum security requirements (see Section 9.2).
			INADEQUATE_SECURITY = 0xc
			# The endpoint requires that HTTP/1.1 be used instead of HTTP/2.
			HTTP_1_1_REQUIRED = 0xd

			# Process a connection error and act accordingly.
			#
			# @return [true, false, nil] returns true if connection handling can continue of false (or nil) for a fatal error.
			def connection_error type
				::Iodine.warn "HTTP/2 error #{type}."
				go_away type
				# case type
				# when NO_ERROR
				# when PROTOCOL_ERROR
				# when INTERNAL_ERROR
				# when FLOW_CONTROL_ERROR
				# when SETTINGS_TIMEOUT
				# when STREAM_CLOSED
				# when FRAME_SIZE_ERROR
				# when REFUSED_STREAM
				# when CANCEL
				# when COMPRESSION_ERROR
				# when CONNECT_ERROR
				# when ENHANCE_YOUR_CALM
				# when INADEQUATE_SECURITY
				# when HTTP_1_1_REQUIRED
				# else
				# end
				# nil
			end

			def self.dispatch
				@dispatch ||= Proc.new do |request|
					case request[:method]
					when 'TRACE'.freeze
						close
						return false
					when 'OPTIONS'.freeze
						response = ::Iodine::Http::Response.new request
						response[:Allow] = 'GET,HEAD,POST,PUT,DELETE,OPTIONS'.freeze
						response['access-control-allow-origin'.freeze] = '*'
						response.finish
						return false
					end
					response = ::Iodine::Http::Response.new request
					begin
						ret = Iodine::Http.on_http.call(request, response)
						if ret.is_a?(String)
							response << ret
						elsif ret == false
							response.clear && (response.status = 404) && (response <<  ::Iodine::Http::Response::STATUS_CODES[404])
						end
						response.finish
					rescue => e
						::Iodine.error e
						# request[:io].emit_frame [INTERNAL_ERROR].pack('N'.freeze), request[:sid], 3
						::Iodine::Http::Response.new(request, 500, {}, ::Iodine::Http::Response::STATUS_CODES[500]).finish
					end
				end
			end
		end
	end
end


