module Iodine

	module Http

		# This class is the part of the Iodine server.
		# The request object is a Hash and the Request provides
		# simple shortcuts and access to the request's Hash data.
		#
		#
		# An Http Request
		class Request < Hash

			# Sets magic cookies - NOT part of the API.
			#
			# magic cookies keep track of both incoming and outgoing cookies, setting the response's cookies as well as the combined cookie respetory (held by the request object).
			#
			# use only the []= for magic cookies. merge and update might not set the response cookies.
			class Cookies < ::Hash
				# sets the Magic Cookie's controller object (which holds the response object and it's `set_cookie` method).
				def set_response response
					@response = response
				end
				# overrides the []= method to set the cookie for the response (by encoding it and preparing it to be sent), as well as to save the cookie in the combined cookie jar (unencoded and available).
				def []= key, val
					return super unless instance_variable_defined?(:@response) && @response
					if key.is_a?(Symbol) && self.has_key?( key.to_s)
						key = key.to_s
					elsif self.has_key?( key.to_s.to_sym)
						key = key.to_s.to_sym
					end
					@response.set_cookie key, (val.nil? ? nil : val)
					super
				end
				# overrides th [] method to allow Symbols and Strings to mix and match
				def [] key
					if key.is_a?(Symbol) && self.has_key?( key.to_s)
						key = key.to_s
					elsif self.has_key?( key.to_s.to_sym)
						key = key.to_s.to_sym
					end
					super
				end
			end

			def initialize io = nil
				super()
				self[:io] = io if io
				self[:cookies] = Cookies.new
				self[:params] = {}
				@parsed = false
			end

			public

			# the request's headers
			def headers
				self.select {|k,v| k.is_a? String }
			end
			# the request's method (GET, POST... etc').
			def request_method
				self[:method]
			end
			# set request's method (GET, POST... etc').
			def request_method= value
				self[:method] = value
			end
			# the parameters sent by the client.
			def params
				self[:params]
			end
			# the cookies sent by the client.
			def cookies
				self[:cookies]
			end

			# the query string
			def query
				self[:query]
			end

			# the original (frozen) path (resource requested).
			def original_path
				self[:original_path]
			end

			# the requested path (rewritable).
			def path
				self[:path]
			end
			def path=(new_path)
				self[:path] = new_path
			end

			# The HTTP version for this request
			def version
				self[:version]
			end

			# the base url ([http/https]://host[:port])
			def base_url switch_scheme = nil
				"#{switch_scheme || self[:scheme]}://#{self[:host_name]}#{self[:port]? ":#{self[:port]}" : ''.freeze}".freeze
			end

			# the request's url, without any GET parameters ([http/https]://host[:port]/path)
			def request_url switch_scheme = nil
				"#{base_url switch_scheme}#{self[:original_path]}".freeze
			end

			# the protocol's scheme (http/https/ws/wss) managing this request
			def scheme
				self[:scheme]
			end

			# the host's name (without the port)
			def host_name
				self[:host_name]
			end

			# @return [true, false] returns true if the requested was an SSL protocol (true also if the connection is clear-text behind an SSL Proxy, such as with some PaaS providers).
			def ssl?
				self[:io].ssl? || self[:scheme] == 'https'.freeze || self[:scheme] == 'wss'.freeze
			end
			alias :secure? :ssl?

			# @return [Iodine::Http, Iodine::Http2, Iodine::Websockets] the Protocol used for the request.
			def io
				self[:io]
			end

			# @return [Hash like storage] Returns the session storage object IF a session was already initialized (use the response to initialize a session).
			def session
				self[:session]
			end

			# method recognition

			HTTP_GET = 'GET'.freeze
			# returns true of the method == GET
			def get?
				self[:method] == HTTP_GET
			end

			HTTP_HEAD = 'HEAD'.freeze
			# returns true of the method == HEAD
			def head?
				self[:method] == HTTP_HEAD
			end
			HTTP_POST = 'POST'.freeze
			# returns true of the method == POST
			def post?
				self[:method] == HTTP_POST
			end
			HTTP_PUT = 'PUT'.freeze
			# returns true of the method == PUT
			def put?
				self[:method] == HTTP_PUT
			end
			HTTP_DELETE = 'DELETE'.freeze
			# returns true of the method == DELETE
			def delete?
				self[:method] == HTTP_DELETE
			end
			HTTP_TRACE = 'TRACE'.freeze
			# returns true of the method == TRACE
			def trace?
				self[:method] == HTTP_TRACE
			end
			HTTP_OPTIONS = 'OPTIONS'.freeze
			# returns true of the method == OPTIONS
			def options?
				self[:method] == HTTP_OPTIONS
			end
			HTTP_CONNECT = 'CONNECT'.freeze
			# returns true of the method == CONNECT
			def connect?
				self[:method] == HTTP_CONNECT
			end
			HTTP_PATCH = 'PATCH'.freeze
			# returns true of the method == PATCH
			def patch?
				self[:method] == HTTP_PATCH
			end
			HTTP_CTYPE = 'content-type'.freeze; HTTP_JSON = /application\/json/.freeze
			# returns true if the request is of type JSON.
			def json?
				self[HTTP_CTYPE] =~ HTTP_JSON
			end
			HTTP_XML = /text\/xml/.freeze
			# returns true if the request is of type XML.
			def xml?
				self[HTTP_CTYPE].match HTTP_XML
			end
			# returns true if this is a websocket upgrade request
			def websocket?
				@is_websocket ||= (self['upgrade'.freeze] && self['upgrade'.freeze].to_s =~ /websocket/i.freeze &&  self['connection'.freeze].to_s =~ /upg/i.freeze && true)
			end
			alias :upgrade? :websocket?

			# parses an HTTP request (quary, body data etc')
			def self.parse request
				# if m = request[:query].match /(([a-z0-9A-Z]+):\/\/)?(([^\/\:]+))?(:([0-9]+))?([^\?\#]*)(\?([^\#]*))?/
					# request[:requested_protocol] = m[1] || request['x-forwarded-proto'] || ( request[:io].ssl? ? 'https' : 'http')
					# request[:host_name] = m[4] || (request['host'] ? request['host'].match(/^[^:]*/).to_s : nil)
					# request[:port] = m[6] || (request['host'] ? request['host'].match(/:([0-9]*)/).to_a[1] : nil)
					# request[:original_path] = HTTP.decode(m[7], :uri) || '/'
					# request['host'] ||= "#{request[:host_name]}:#{request[:port]}"

					# # parse query for params - m[9] is the data part of the query
					# if m[9]
					# 	extract_params m[9].split(/[&;]/), request[:params]
					# end
				# end
				return request if request[:client_ip]

				request.delete :headers_size

				# 2014 RFC 7239 Forwarded: for=192.0.2.60; proto=http; by=203.0.113.43
				if tmp = request['forwarded'.freeze]
					tmp = tmp.join(';'.freeze) if tmp.is_a?(Array)
					tmp.match(/proto=([^\s;]+)/i.freeze).tap {|m| request[:scheme] = m[1].downcase if m } unless request[:scheme]
					tmp.match(/for=[\[]?[\"]?([^\s;\",]+)/i.freeze).tap {|m| request[:client_ip] = m[1] if m } unless request[:client_ip]
				end

				request[:client_ip] ||= (request['x-forwarded-for'.freeze].to_s.match(/[^\s\[\"\,]+/.freeze) || request[:io].io.to_io.remote_address.ip_address).to_s rescue 'unknown'.freeze
				request[:version] ||= '1'.freeze

				request[:scheme] ||= request['x-forwarded-proto'.freeze] ? request['x-forwarded-proto'.freeze].downcase : ( request[:io].ssl? ? 'https'.freeze : 'http'.freeze)
				tmp = (request['host'.freeze] || request[:authority] || ''.freeze).split(':'.freeze)
				request[:host_name] = tmp[0]
				request[:port] = tmp[1]

				tmp = (request[:query] ||= request[:path] ).split('?'.freeze, 2)
				request[:path] = tmp[0].chomp('/'.freeze)
				request[:original_path] = tmp[0].freeze
				request[:quary_params] = tmp[1]
				extract_params tmp[1].split(/[&;]/.freeze), (request[:params] ||= {}) if tmp[1]

				if request['cookie'.freeze]
					if request['cookie'.freeze].is_a?(Array)
						tmp = []
						request['cookie'.freeze].each {|s| s.split(/[;,][\s]?/.freeze).each { |c| tmp << c } }
						request['cookie'.freeze] = tmp
						extract_header tmp, request.cookies
					else
						extract_header request['cookie'.freeze].split(/[;,][\s]?/.freeze), request.cookies
					end
				elsif request['set-cookie'.freeze]
					request['set-cookie'.freeze] = [ request['set-cookie'.freeze] ] unless request['set-cookie'.freeze].is_a?(Array)
					tmp = []
					request['set-cookie'.freeze].each {|s| tmp << s.split(/[;][\s]?/.freeze)[0] }
					request['set-cookie'.freeze] = tmp
					extract_header tmp, request.cookies
				end

				read_body request if request[:body]

				request
			end

			# re-encodes a string into UTF-8
			def self.make_utf8!(string, encoding= ::Encoding::UTF_8)
				return false unless string
				string.force_encoding(::Encoding::ASCII_8BIT).encode!(encoding, ::Encoding::ASCII_8BIT, invalid: :replace, undef: :replace, replace: ''.freeze) unless string.force_encoding(encoding).valid_encoding?
				string
			end

			# re-encodes a string into UTF-8
			def self.try_utf8!(string, encoding= ::Encoding::UTF_8)
				return false unless string
				string.force_encoding(::Encoding::ASCII_8BIT) unless string.force_encoding(encoding).valid_encoding?
				string
			end

			# encodes URL data.
			def self.encode_url str
				(str.to_s.gsub(/[^a-z0-9\*\.\_\-]/i.freeze) {|m| '%%%02x'.freeze % m.ord }).force_encoding(::Encoding::ASCII_8BIT)
			end

			# Adds paramaters to a Hash object, according to the Iodine's server conventions.
			def self.add_param_to_hash name, value, target, &block
				begin
					c = target
					val = rubyfy! value
					a = name.chomp('[]'.freeze).split('['.freeze)

					a[0...-1].inject(target) do |h, n|
						n.chomp!(']'.freeze);
						n.strip!;
						raise "malformed parameter name for #{name}" if n.empty?
						n = (n.to_i.to_s == n) ?  n.to_i : n.to_sym            
						c = (h[n] ||= {})
					end
					n = a.last
					n.chomp!(']'.freeze); n.strip!;
					n = n.empty? ? nil : ( (n.to_i.to_s == n) ?  n.to_i : n.to_sym )
					if n
						if c[n]
							c[n].is_a?(Array) ? (c[n] << val) : (c[n] = [c[n], val])
						else
							c[n] = val
						end
						c.default_proc = block if block
					else
						if c[n]
							c[n].is_a?(Array) ? (c[n] << val) : (c[n] = [c[n], val])
						else
							c[n] = [val]
						end
					end
					val
				rescue => e
					Iodine.error e
					Iodine.error "(Silent): parameters parse error for #{name} ... maybe conflicts with a different set?"
					target[name] = val
				end
			end

			# extracts parameters from the query
			def self.extract_params data, target_hash
				data.each do |set|
					list = set.split('='.freeze, 2)
					list.each {|s| uri_decode!(s) if s}
					add_param_to_hash list.shift, list.shift, target_hash
				end
			end
			# decode form / uri data (including the '+' sign as a space (%20) replacement).
			def self.uri_decode! s
				s.gsub!('+'.freeze, '%20'.freeze); s.gsub!(/\%[0-9a-f]{2}/i.freeze) {|m| m[1..2].to_i(16).chr}; s.gsub!(/&#[0-9]{4};/i.freeze) {|m| [m[2..5].to_i].pack 'U'.freeze }; s
			end
			# extracts parameters from header data 
			def self.extract_header data, target_hash
				data.each do |set|
					list = set.split('='.freeze, 2)
					list.each {|s| form_decode!(s) if s}
					add_param_to_hash list.shift, list.shift, target_hash
				end
			end
			# decode percent-encoded data (excluding the '+' sign for encoding).
			def self.form_decode! s
				s.gsub!(/\%[0-9a-f]{2}/i.freeze) {|m| m[1..2].to_i(16).chr}; s.gsub!(/&#[0-9]{4};/i.freeze) {|m| [m[2..5].to_i].pack 'U'.freeze }; s
			end
			# Changes String to a Ruby Object, if it's a special string...
			def self.rubyfy!(string)
				return string unless string.is_a?(String)
				try_utf8! string
				if string == 'true'.freeze
					string = true
				elsif string == 'false'.freeze
					string = false
				elsif string.to_i.to_s == string
					string = string.to_i
				end
				string
			end

			# read the body's data and parse any incoming data.
			def self.read_body request
				# save body for Rack, if applicable
				# request[:rack_input] = request[:body]  if ::Iodine::Http.on_http == ::Iodine::Http::Rack
				# parse content
				request[:body].rewind
				case request['content-type'.freeze].to_s
				when /x-www-form-urlencoded/.freeze
					extract_params request[:body].read.split(/[&;]/.freeze), request[:params] #, :form # :uri
				when /multipart\/form-data/.freeze
					read_multipart request, request
				when /text\/xml/.freeze
					# to-do support xml?
					# request[:xml] = make_utf8! request[:body].read
					nil
				when /application\/json/.freeze
					JSON.parse(make_utf8! request[:body].read).each {|k, v| add_param_to_hash k, v, request[:params]} rescue true
				end
				request[:body].rewind if request[:body]
			end

			# parse a mime/multipart body or part.
			def self.read_multipart request, headers = {}, boundary = [], name_prefix = String.new
				body = request[:body]
				return unless headers['content-type'.freeze].to_s =~ /multipart/i.freeze
				part_headers = {}
				extract_header headers['content-type'.freeze].split(/[;,][\s]?/.freeze), part_headers
				boundary << part_headers[:boundary]
				if part_headers[:name]
					if name_prefix.empty?
						name_prefix << part_headers[:name]
					else
						name_prefix << "[#{part_headers[:name]}]".freeze
					end
				end
				part_headers.delete :name
				part_headers.clear
				line = nil
				boundary_length = nil
				true until ( (line = body.gets) ) && line =~ /\A--(#{boundary.join '|'})(--)?[\r]?\n/
				until body.eof?
					return if line =~ /--[\r]?\n/.freeze
					return boundary.pop if boundary.count > 1 && line.match(/--(#{boundary.join '|'})/)[1] != boundary.last
					boundary_length = line.bytesize
					line = body.gets until line.nil? || line =~ /\:/.freeze
					until line.nil? || line =~ /^[\r]?\n/.freeze
						tmp = line.strip.split ':'.freeze, 2
						return Iodine.error "Http multipart parsing error (multipart header data malformed): #{line}" unless tmp && tmp.count == 2
						tmp[0].strip!; tmp[0].downcase!; tmp[1].strip!; 
						part_headers[tmp[0]] = tmp[1]
						line = body.gets
					end
					return if line.nil?
					if !part_headers['content-disposition'.freeze]
						Iodine.error "Wrong multipart format with headers: #{part_headers}"
						return
					end
					extract_header part_headers['content-disposition'.freeze].split(/[;,][\s]?/.freeze), part_headers
					if name_prefix.empty?
						name = part_headers[:name][1..-2]
					else
						name = "#{name_prefix}[part_headers[:name][1..-2]}]"
					end
					part_headers.delete :name

					start_part_pos = body.pos
					tmp = /\A--(#{boundary.join '|'})(--)?[\r]?\n/
					line.clear until ( (line = body.gets) &&  line =~ tmp)
					end_part_pos = (body.pos - line.bytesize) - 2
					new_part_pos = body.pos 
					body.pos = end_part_pos
					end_part_pos += 1 unless body.getc =~ /[\r\n]/.freeze
					end_part_pos += 1 unless body.getc =~ /[\r\n\-]/.freeze
					if part_headers['content-type'.freeze]
						if part_headers['content-type'.freeze] =~ /multipart/i.freeze
							body.pos = start_part_pos
							read_multipart request, part_headers, boundary, name_prefix
						else
							part_headers.delete 'content-disposition'.freeze
							add_param_to_hash "#{name}[type]", make_utf8!(part_headers['content-type'.freeze]), request[:params]
							part_headers.each {|k,v|  add_param_to_hash "#{name}[#{k.to_s}]", make_utf8!(v[0] == '"' ? v[1..-2].to_s : v), request[:params] if v}

							tmp = Tempfile.new 'upload'.freeze, encoding: 'binary'.freeze
							body.pos = start_part_pos
							((end_part_pos - start_part_pos)/65_536).to_i.times {tmp << body.read(65_536)} 
							tmp << body.read(end_part_pos - body.pos)
							add_param_to_hash "#{name}[size]", tmp.size, request[:params]
							add_param_to_hash "#{name}[file]", tmp, request[:params] do |hash, key|
								if key == :data || key == "data".freeze && hash.has_key?(:file) && hash[:file].is_a?(::Tempfile)
									hash[:file].rewind
									(hash[:data] = hash[:file].read)
								end
							end
							tmp.rewind
						end
					else
						body.pos = start_part_pos
						add_param_to_hash name, form_decode!( body.read(end_part_pos - start_part_pos) ), request[:params] 
					end
					body.pos = new_part_pos
				end

			end

		end
	end
end