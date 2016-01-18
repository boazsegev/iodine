module Iodine
	module Http
		# this class handles Http responses.
		#
		# The response can be sent in stages but should complete within the scope of the connecton's message. Please notice that headers and status cannot be changed once the response started sending data.
		class Response
			# Makes sure that the `flash` cookie-jar doesn't have Symbols and Strings overlapping.
			class Flash < ::Hash
				# overrides the []= method to set the cookie for the response (by encoding it and preparing it to be sent), as well as to save the cookie in the combined cookie jar (unencoded and available).
				def []= key, val
					if key.is_a?(Symbol) && self.has_key?( key.to_s)
						key = key.to_s
					elsif self.has_key?( key.to_s.to_sym)
						key = key.to_s.to_sym
					end
					super
				end
				# overrides th [] method to allow Symbols and Strings to mix and match
				def [] key
					if key.is_a?(Symbol) && self.has_key?( key.to_s)
						key = key.to_s
					elsif self.has_key?( key.to_s.to_sym)
						key = key.to_s.to_sym
					elsif self.has_key? "magic_flash_#{key.to_s}".freeze.to_sym
						key = "magic_flash_#{key.to_s}".freeze.to_sym
					end
					super
				end
			end

			# the response's status code
			attr_accessor :status
			# the response's headers
			attr_reader :headers
			# the flash cookie-jar (single-use cookies, that survive only one request).
			attr_reader :flash
			# the response's body buffer container (an array). This object is removed once the headers are sent and all write operations hang after that point.
			attr_accessor :body
			# the request.
			attr_accessor :request
			# Logs the number of bytes written.
			attr_accessor :bytes_written
			# forces the connection to remain alive if this flag is set to `true` (otherwise follows Http and optimization guidelines).
			attr_accessor :keep_alive

			# the response object responds to a specific request on a specific io.
			# hence, to initialize a response object, a request must be set.
			#
			# use, at the very least `HTTPResponse.new request`
			def initialize request, status = 200, headers = {}, content = nil
				@request = request
				@status = status
				@headers = headers
				@body = content || []
				@request.cookies.set_response self
				@cookies = {}
				@bytes_written = 0
				@keep_alive = @http_sblocks_count = false
				# propegate flash object
				@flash = ::Iodine::Http::Response::Flash.new
				request.cookies.each do |k,v|
					@flash[k] = v if k.to_s.start_with? 'magic_flash_'.freeze
				end
			end

			# returns the active protocol for the request.
			def io
				@request[:io]
			end

			# returns true if headers were already sent
			def headers_sent?
				@headers.frozen?
			end

			# Creates a streaming block. Once all streaming blocks are done, the response will automatically finish.
			#
			# This avoids manualy handling {#start_streaming}, {#finish_streaming} and asynchronously tasking.
			#
			# Every time data is sent the timout is reset. Responses longer than timeout will not be sent (but they will be processed).
			#
			# Since Iodine is likely to be multi-threading (depending on your settings and architecture), it is important that
			# streaming blocks are nested rather than chained. Chained streaming blocks might be executed in parallel and 
			# suffer frome race conditions that might lead to the response being corrupted.
			#
			# Accepts a required block. i.e.
			#
			#     response.stream_async {sleep 1; response << "Hello Streaming"}
			#     # OR, you can nest (but not chain) the streaming calls
			#     response.stream_async do
			#       sleep 1
			#       response << "Hello Streaming"
			#       response.stream_async do
			#           sleep 1
			#           response << "\r\nGoodbye Streaming"
			#       end
			#     end
			#
			# @return [true, Exception] The method returns immidiatly with a value of true unless it is impossible to stream the response (an exception will be raised) or a block wasn't supplied.
			def stream_async &block
				raise "Block required." unless block
				start_streaming unless @http_sblocks_count
				@http_sblocks_count += 1
				@stream_proc ||= Proc.new { |block| raise "IO closed. Streaming failed." if request[:io].io.closed?; block.call; @http_sblocks_count -= 1; finish_streaming }
				Iodine.run block, &@stream_proc
			end

			# Creates nested streaming blocks for an Array object (an object answering `#shift`). Once all streaming blocks are done, the response will automatically finish.
			#
			# Since streaming blocks might run in parallel, nesting the streaming blocks is important...
			#
			# However, manually nesting hundreds of nesting blocks is time consuming and error prone.
			#
			# {.sream_enum} allows you to stream an enumerable knowing that Plezi will nest the streaming blocks dynamically.
			#
			# Accepts:
			# enum:: an Enumerable or an object that answers to the `to_a` method (the array will be used to stream the )
			#
			# If an Array is passed to the enumerable, it will be changed and emptied as the streaming progresses.
			# So, if preserving the array is important, please create a shallow copy of the array first using the `.dup` method.
			#
			# i.e.:
			#
			#      data = "Hello world!".chars
			#      response.stream_enum(data.each_with_index) {|c, i| response << c; sleep i/10.0 }
			#
			#
			# @return [true, Exception] The method returns immidiatly with a value of true unless it is impossible to stream the response (an exception will be raised) or a block wasn't supplied.
			def stream_array enum, &block
				enum = enum.to_a
				return if enum.empty?
				stream_async do
					args = enum.shift
					block.call(*args)
					stream_array enum, &block
				end
			end

			# Creates and returns the session storage object.
			#
			# By default and for security reasons, session id's created on a secure connection will NOT be available on a non secure connection (SSL/TLS).
			#
			# Since this method renews the session_id's cookie's validity (update's it's times-stump), it must be called for the first time BEFORE the headers are sent.
			#
			# After the session object was created using this method call, it should be safe to continue updating the session data even after the headers were sent and this method would act as an accessor for the already existing session object.
			#
			# @return [Hash like storage] creates and returns the session storage object with all the data from a previous connection.
			def session
				return @session if instance_variable_defined?(:@session) && @session
				if @request.ssl?
					@@sec_session_token ||= "#{::Iodine::Http.session_token}_enc".freeze
					id = @request.cookies[@@sec_session_token.to_sym] || SecureRandom.uuid
					set_cookie @@sec_session_token, id, expires: :session, secure: true, http_only: true
				else
				id = @request.cookies[::Iodine::Http.session_token.to_sym] || SecureRandom.uuid
					set_cookie ::Iodine::Http.session_token, id, expires: :session, http_only: true
				end
				@request[:session] = @session = ::Iodine::Http::SessionManager.get(id)
			end

			# Returns the OLD session storage object when the connection was upgraded to SSL.
			#
			# By default and for security reasons, session id's created on a secure connection will NOT be available on a non secure connection (SSL/TLS).
			# However, while upgrading to the encrypted connection, the non_encrypted session storage is still available for review.
			#
			# @return [nil, "Hash like storage"] returns the non-encypeted  connection's session storage object, if it exists. This method will NOT create a new sesssion if it didn't exist.
			def session_old
				::Iodine::Http::SessionManager.get(@request.cookies[::Iodine::Http.session_token.to_sym]) if @request.cookies[::Iodine::Http.session_token.to_sym]
			end

			# Returns a writable combined hash of the request's cookies and the response cookie values.
			#
			# Any cookies writen to this hash (`response.cookies[:name] = value` will be set using default values).
			#
			# It's also possible to use this combined hash to delete cookies, using: response.cookies[:name] = nil
			def cookies
				@request.cookies
			end

			# Returns the response's encoded cookie hash.
			#
			# This method allows direct editing of the cookies about to be set.
			def raw_cookies
				@cookies
			end

			# pushes data to the buffer of the response. this is the preferred way to add data to the response.
			#
			# If the headers were already sent, this will also send the data and hang until the data was sent.
			def << str
				( @body ? @body.push(str) : ( (@body = str.dup) && request[:io].stream_response(self) ) ) if str
				self
			end

			# returns a response header, if set.
			def [] header
				header.is_a?(String) ? (header.frozen? ? header : header.downcase!) : (header.is_a?(Symbol) ? (header = header.to_s.downcase) : (return false))
				headers[header]
			end

			# Sets a response header. response headers should be a **downcase** String (not a symbol or any other object).
			#
			# this is the prefered to set a header.
			#
			# Be aware that HTTP/2 will treat a header name with an upper-case letter as an Error! (while HTTP/1.1 ignores the letter case)
			#
			# returns the value set for the header.
			#
			# see HTTP response headers for valid headers and values: http://en.wikipedia.org/wiki/List_of_HTTP_header_fields
			def []= header, value
				raise 'Cannot set headers after the headers had been sent.' if headers_sent?
				return (@headers.delete(header) && nil) if header.nil?
				header.is_a?(String) ? (header.frozen? ? header : header.downcase!) : (header.is_a?(Symbol) ? (header = header.to_s.downcase) : (return false))
				headers[header]	= value
			end


			COOKIE_NAME_REGEXP = /[\x00-\x20\(\)\<\>@,;:\\\"\/\[\]\?\=\{\}\s]/.freeze

			# Sets/deletes cookies when headers are sent.
			#
			# Accepts:
			# name:: the cookie's name
			# value:: the cookie's value
			# parameters:: a parameters Hash for cookie creation.
			#
			# Parameters accept any of the following Hash keys and values:
			#
			# expires:: a Time object with the expiration date. defaults to 10 years in the future.
			# max_age:: a Max-Age HTTP cookie string.
			# path:: the path from which the cookie is acessible. defaults to '/'.
			# domain:: the domain for the cookie (best used to manage subdomains). defaults to the active domain (sub-domain limitations might apply).
			# secure:: if set to `true`, the cookie will only be available over secure connections. defaults to false.
			# http_only:: if true, the HttpOnly flag will be set (not accessible to javascript). defaults to false.
			#
			# Setting the request's coockies (`request.cookies[:name] = value`) will automatically call this method with default parameters.
			#
			def set_cookie name, value, params = {}
				raise 'Cannot set cookies after the headers had been sent.' if headers_sent?
				if value.is_a?(Hash) && value.has_key?(:value) && params.empty?
					params = value
					value = params.delete :value
				end
				name = name.to_s
				raise 'Illegal cookie name' if name =~ COOKIE_NAME_REGEXP
				if value.nil?
					params[:expires] = (Iodine.time - 315360000)
					value = 'deleted'.freeze					
				else
					params[:expires] ||= (Iodine.time + 315360000) unless params[:max_age]
				end
				params[:path] ||= '/'.freeze
				value = Iodine::Http::Request.encode_url(value) # this dups the string
				if params[:max_age]
					value << ('; Max-Age=%s'.freeze % params[:max_age])
				else
					value << ('; Expires=%s'.freeze % params[:expires].httpdate) if params[:expires].is_a?(::Time)
				end
				value << "; Path=#{params[:path]}".freeze
				value << "; Domain=#{params[:domain]}".freeze if params[:domain]
				value << '; Secure'.freeze if params[:secure]
				value << '; HttpOnly'.freeze if params[:http_only]
				@cookies[name.to_sym] = value
			end

			# deletes a cookie (actually calls `set_cookie name, nil`)
			def delete_cookie name
				set_cookie name, nil
			end

			# clears the response object, unless headers were already sent (the response is already on it's way, at least in part).
			#
			# returns false if the response was already sent.
			def clear
				return false if @headers.frozen?
				@status, @body, @headers, @cookies = 200, [], {}, {}
				self
			end

			# attempts to write a non-streaming response to the IO. This can be done only once and will quitely fail subsequently.
			def finish
				request[:io].send_response self
				request.delete(:body).tap {|f| f.close unless f.respond_to?(:close) && f.closed? rescue false } if request[:body] && @http_sblocks_count.to_i == 0
			end

			# Returns the connection's LOCAL UUID.
			def uuid
				request[:io].id
			end

			# Sets the response to redirect to a different page.
			#
			# The method accepts:
			# url:: a String containing the URL to which the response forwards. `nil` or an empty string will be replaced with: `request.base_url`.
			# options:: an options Hash. Any key-value pairs not supported WILL be used as flash cookie name-value pairs.
			#
			# The option's hash includes the following options:
			# permanent:: a `true`/`false` value stating the redirection type. Defaults to `false` (temporary redirection).
			# *:: remember, all other key-value pairs WILL be used as flash cookie name-value pairs.
			def redirect_to url, options = {}
				raise 'Cannot redirect after headers were sent.' if headers_sent?
				url = "#{@request.base_url}/".freeze if url.nil? || (url.is_a?(String) && url.empty?)
				raise TypeError, 'URL must be a String.' unless url.is_a?(String)
				url = url_for(url) unless url.is_a?(String) || url.nil?
				# redirect
				@status = options.delete(:permanent) ? 301 : 302
				self['location'] = url
				@flash.update options
				true
			end

			# response status codes, as defined.
			STATUS_CODES = {100=>"Continue".freeze,
				101=>"Switching Protocols".freeze,
				102=>"Processing".freeze,
				200=>"OK".freeze,
				201=>"Created".freeze,
				202=>"Accepted".freeze,
				203=>"Non-Authoritative Information".freeze,
				204=>"No Content".freeze,
				205=>"Reset Content".freeze,
				206=>"Partial Content".freeze,
				207=>"Multi-Status".freeze,
				208=>"Already Reported".freeze,
				226=>"IM Used".freeze,
				300=>"Multiple Choices".freeze,
				301=>"Moved Permanently".freeze,
				302=>"Found".freeze,
				303=>"See Other".freeze,
				304=>"Not Modified".freeze,
				305=>"Use Proxy".freeze,
				306=>"(Unused)".freeze,
				307=>"Temporary Redirect".freeze,
				308=>"Permanent Redirect".freeze,
				400=>"Bad Request".freeze,
				401=>"Unauthorized".freeze,
				402=>"Payment Required".freeze,
				403=>"Forbidden".freeze,
				404=>"Not Found".freeze,
				405=>"Method Not Allowed".freeze,
				406=>"Not Acceptable".freeze,
				407=>"Proxy Authentication Required".freeze,
				408=>"Request Timeout".freeze,
				409=>"Conflict".freeze,
				410=>"Gone".freeze,
				411=>"Length Required".freeze,
				412=>"Precondition Failed".freeze,
				413=>"Payload Too Large".freeze,
				414=>"URI Too Long".freeze,
				415=>"Unsupported Media Type".freeze,
				416=>"Range Not Satisfiable".freeze,
				417=>"Expectation Failed".freeze,
				422=>"Unprocessable Entity".freeze,
				423=>"Locked".freeze,
				424=>"Failed Dependency".freeze,
				426=>"Upgrade Required".freeze,
				428=>"Precondition Required".freeze,
				429=>"Too Many Requests".freeze,
				431=>"Request Header Fields Too Large".freeze,
				500=>"Internal Server Error".freeze,
				501=>"Not Implemented".freeze,
				502=>"Bad Gateway".freeze,
				503=>"Service Unavailable".freeze,
				504=>"Gateway Timeout".freeze,
				505=>"HTTP Version Not Supported".freeze,
				506=>"Variant Also Negotiates".freeze,
				507=>"Insufficient Storage".freeze,
				508=>"Loop Detected".freeze,
				510=>"Not Extended".freeze,
				511=>"Network Authentication Required".freeze
			}

			# This will return the Body object as an IO like object, such as StringIO (or File) and set the body to `nil` (seeing as it was extracted from the response).
			#
			# This method will also attempts to set headers and update the response status in relation to the body, if applicable. Call this BEFORE getting any final data about the response or sending the headers.
			def extract_body
				body_io = if @body.is_a?(Array)
					return (@body = nil) if body.empty?
					StringIO.new @body.join
				elsif @body.is_a?(String)
					return (@body = nil) if body.empty?
					StringIO.new @body
				elsif @body.nil?
					return nil
					nil
				elsif  @body.is_a?(File) || @body.is_a?(Tempfile) || @body.is_a?(StringIO)
					@body
				elsif @body.respond_to? :each
					tmp = String.new
					@body.each {|s| tmp << s}
					@body.close if @body.respond_to? :close
					@body = nil
					return nil if tmp.empty? 
					StringIO.new tmp
				end
				@body = nil
				body_io.rewind

				if !(@headers.frozen?) && @request['range'.freeze] && @request.get? && @status == 200 && @headers['content-length'.freeze].nil?
					r = @request['range'.freeze].match(/^bytes=([\d]+)\-([\d]+)?$/i.freeze)
					if r
						old_size = body_io.size
						start_pos = r[1].to_i
						end_pos = (r[2] || (old_size - 1)).to_i
						read_length = end_pos-start_pos+ 1
						@status = 206 unless old_size == read_length
						body_io.pos = start_pos
						unless end_pos == old_size-1
							new_body = body_io.read(read_length)
							body_io.close
							body_io = StringIO.new new_body
							body_io.rewind
						end
						@headers['content-range'.freeze] = "bytes #{start_pos}-#{end_pos}/#{old_size}"
						@headers['accept-ranges'.freeze] ||= 'bytes'.freeze
					else
						@headers['accept-ranges'.freeze] ||= 'none'.freeze
					end
				end

				body_io
			end

			# This will return an array of cookie settings to be appended to `set-cookie` headers.
			def extract_cookies
				unless @cookies.frozen?
					# remove old flash cookies
					@request.cookies.keys.each do |k|
						if k.to_s.start_with? 'magic_flash_'.freeze
							set_cookie k, nil
							flash.delete k
						end
					end
					#set new flash cookies
					@flash.each do |k,v|
						set_cookie "magic_flash_#{k.to_s}".freeze, v
					end
					@cookies.freeze
					# response.cookies.set_response nil
					@flash.freeze
				end
				arr = []
				@cookies.each {|k, v| arr << "#{k.to_s}=#{v.to_s}"}
				arr
			end

			protected

			# Sets the http streaming flag and sends the responses headers, so that the response could be handled asynchronously.
			#
			# if this flag is not set, the response will try to automatically finish its job
			# (send its data and maybe close the connection).
			#
			# NOTICE! :: If HTTP streaming is set, you will need to manually call `response.finish_streaming`
			# or the connection will not close properly and the client will be left expecting more information.
			def start_streaming
				raise "Cannot start streaming after headers were sent!" if headers_sent?
				@http_sblocks_count ||= 0
				request[:io].stream_response self
			end

			# Sends the complete response signal for a streaming response.
			#
			# Careful - sending the completed response signal more than once might case disruption to the HTTP connection.
			def finish_streaming
				return unless @http_sblocks_count == 0
				request[:io].stream_response self, true
				request.delete(:body).tap {|f| f.close unless f.respond_to?(:close) && f.closed? rescue false } if request[:body]
			end
		end
	end
end


