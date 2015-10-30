module Iodine
	module Http
		class Http1 < ::Iodine::Protocol
			def on_open
				set_timeout 1
				@refuse_requests = false
				@bytes_sent = 0
				@parser = {}
			end
			def on_message data
					return if @refuse_requests
					@http2_pri_review ||= ( ::Iodine::Http.http2 && ::Iodine::Http::Http2.pre_handshake(self, data) && (return true) ) || true

					data = ::StringIO.new data
					until data.eof?
						request = (@request ||= ::Iodine::Http::Request.new(self))
						unless request[:method]
							l = data.gets.strip
							if l.bytesize > 16_384
								write "HTTP/1.0 414 Request-URI Too Long\r\ncontent-length: 20\r\n\r\nRequest URI too Long"
								Iodine.warn "Http/1 URI too long, closing connection."
								return close
							end
							next if l.empty?
							request[:method], request[:query], request[:version] = l.split(/[\s]+/, 3)
							return (Iodine.warn('Htt1 Protocol Error, closing connection.') && close) unless request[:method] =~ HTTP_METHODS_REGEXP
							request[:version] = (request[:version] || '1.1'.freeze).match(/[\d\.]+/)[0]
							request[:time_recieved] = Time.now
						end
						until request[:headers_complete] || (l = data.gets).nil?
							if l.include? ':'
								# n = l.slice!(0, l.index(':')); l.slice! 0
								# n.strip! ; n.downcase!; n.freeze
								# request[n] ? (request[n].is_a?(Array) ? (request[n] << l) : request[n] = [request[n], l ]) : (request[n] = l)
								request[:headers_size] ||= 0
								request[:headers_size] += l.bytesize
								l = l.strip.split(/:[\s]?/, 2)
								l[0].strip! ; l[0].downcase!;
								request[l[0]] ? (request[l[0]].is_a?(Array) ? (request[l[0]] << l[1]) : request[l[0]] = [request[l[0]], l[1] ]) : (request[l[0]] = l[1])
							elsif l =~ /^[\r]?\n/
								request[:headers_complete] = true
							else
								#protocol error
								Iodine.warn 'Protocol Error, closing connection.'
								return close
							end
							if request.length > 2096 || request[:headers_size] > 262_144
								write "HTTP/1.0 431 Request Header Fields Too Large\r\ncontent-length: 31\r\n\r\nRequest Header Fields Too Large"
								return (Iodine.warn('Http1 header overloading, closing connection.') && close)
							end
						end
						until request[:body_complete] && request[:headers_complete]
							if request['transfer-coding'.freeze] == 'chunked'.freeze
								# ad mid chunk logic here
								if @parser[:length].to_i == 0
									chunk = data.gets
									return false unless chunk
									@parser[:length] = chunk.to_i(16)
									return (Iodine.warn('Protocol Error, closing connection.') && close) unless @parser[:length]
									request[:body_complete] = true && break if @parser[:length] == 0
									@parser[:act_length] = 0
									request[:body] ||= Tempfile.new('iodine'.freeze, :encoding => 'binary'.freeze)
								end
								chunk = data.read(@parser[:length] - @parser[:act_length])
								return false unless chunk
								request[:body] << chunk
								@parser[:act_length] += chunk.bytesize
								(@parser[:act_length] = @parser[:length] = 0) && (data.gets) if @parser[:act_length] >= @parser[:length]
							elsif request['content-length'.freeze] && request['content-length'.freeze].to_i != 0
								request[:body] ||= Tempfile.new('iodine'.freeze, :encoding => 'binary'.freeze)
								packet = data.read(request['content-length'.freeze].to_i - request[:body].size)
								return false unless packet
								request[:body] << packet
								request[:body_complete] = true if request['content-length'.freeze].to_i - request[:body].size <= 0
							elsif request['content-type'.freeze]
								Iodine.warn 'Body type protocol error.' unless request[:body]
								line = data.gets
								return false unless line
								(request[:body] ||= Tempfile.new('iodine'.freeze, :encoding => 'binary'.freeze) ) << line
								request[:body_complete] = true if line =~ EOHEADERS
							else
								request[:body_complete] = true
							end
						end
						if request[:body] && request[:body].size > ::Iodine::Http.max_http_buffer
							Iodine.warn("Http1 message body too big, closing connection (Iodine::Http.max_http_buffer == #{::Iodine::Http.max_http_buffer} bytes) - #{request[:body].size} bytes.")
							request.delete(:body).tap {|f| f.close unless f.closed? } rescue false
							write "HTTP/1.0 413 Payload Too Large\r\ncontent-length: 17\r\n\r\nPayload Too Large"
							return close
						end
						(@request = ::Iodine::Http::Request.new(self)) && ( (::Iodine::Http.http2 && ::Iodine::Http::Http2.handshake(request, self, data)) || dispatch(request, data) ) if request.delete :body_complete
					end
			end

			def send_response response
				return false if response.headers.frozen?

				body = response.extract_body
				request = response.request
				headers = response.headers

				headers['content-length'.freeze] ||= body.size if body

				keep_alive = response.keep_alive
				if (request[:version].to_f > 1 && request['connection'.freeze].nil?) || request['connection'.freeze].to_s =~ /ke/i || (headers['connection'.freeze] && headers['connection'.freeze] =~ /^ke/i)
					keep_alive = true
					headers['connection'.freeze] ||= 'keep-alive'.freeze
					headers['keep-alive'.freeze] ||= "timeout=#{(@timeout ||= 3).to_s}"
				else
					headers['connection'.freeze] ||= 'close'.freeze
				end

				send_headers response
				return log_finished(response) && (body && body.close) if request.head? || body.nil?
				until body.eof?
					written = write(body.read 65_536)
					return Iodine.warn("Http/1 couldn't send response because connection was lost.") && body.close unless written
					response.bytes_written += written
				end
				body.close
				close unless keep_alive
				log_finished response
			end
			def stream_response response, finish = false
				timeout = 15
				unless response.headers.frozen?
					response['transfer-encoding'.freeze] = 'chunked'
					response.headers['connection'.freeze] = 'close'.freeze
					send_headers response
					@refuse_requests = true
				end
				return if response.request.head?
				body = response.extract_body
				until body.eof?
					written = stream_data(body.read 65_536)
					return Iodine.warn("Http/1 couldn't send response because connection was lost.") && body.close unless written
					response.bytes_written += written
				end if body
				if finish
					response.bytes_written += stream_data('')
					log_finished response
					close unless response.keep_alive
				end
				body.close if body
				true
			end

			protected

			HTTP_METHODS = %w{GET HEAD POST PUT DELETE TRACE OPTIONS CONNECT PATCH}
			HTTP_METHODS_REGEXP = /\A#{HTTP_METHODS.join('|')}/i

			def dispatch request, data
				return data.string.clear if @io.closed? || @refuse_requests
				::Iodine::Http::Request.parse request
				#check for server-responses
				case request[:method]
				when 'TRACE'.freeze
					close
					data.string.clear
					return false
				when 'OPTIONS'.freeze
					response = ::Iodine::Http::Response.new request
					response[:Allow] = 'GET,HEAD,POST,PUT,DELETE,OPTIONS'.freeze
					response['access-control-allow-origin'.freeze] = '*'
					response['content-length'.freeze] = 0
					send_response response
					return false
				end
				response = ::Iodine::Http::Response.new request
				begin
					if request.websocket?
						@refuse_requests = true
						::Iodine::Http::Websockets.handshake request, response, ::Iodine::Http.on_websocket.call(request, response)
					else
						ret = ::Iodine::Http.on_http.call(request, response)
						if ret.is_a?(String)
							response << ret
						elsif ret == false
							response.clear && (response.status = 404) && (response <<  ::Iodine::Http::Response::STATUS_CODES[404])
						end
					end
					send_response response
				rescue => e
					Iodine.error e
					send_response ::Iodine::Http::Response.new(request, 500, {},  ::Iodine::Http::Response::STATUS_CODES[500])
				end
			end

			def send_headers response
				return false if response.headers.frozen?
				request = response.request
				headers = response.headers

				# response['date'.freeze] ||= request[:time_recieved].httpdate

				out = "HTTP/#{request[:version]} #{response.status} #{::Iodine::Http::Response::STATUS_CODES[response.status] || 'unknown'}\r\n"

				out << request[:time_recieved].utc.strftime("Date: %a, %d %b %Y %H:%M:%S GMT\r\n".freeze) unless headers['date'.freeze]

				# unless @headers['connection'] || (@request[:version].to_f <= 1 && (@request['connection'].nil? || !@request['connection'].match(/^k/i))) || (@request['connection'] && @request['connection'].match(/^c/i))
				headers.each {|k,v| out << "#{k.to_s}: #{v}\r\n"}
				out << "cache-control: max-age=0, no-cache\r\n".freeze unless headers['cache-control'.freeze]
				response.extract_cookies.each {|cookie| out << "set-cookie: #{cookie}\r\n"}
				out << "\r\n"

				response.bytes_written += (write(out) || 0)
				out.clear
				headers.freeze
				response.raw_cookies.freeze
			end
			def stream_data data = nil
				 write("#{data.to_s.bytesize.to_s(16)}\r\n#{data.to_s}\r\n")
			end

			def log_finished response
				@bytes_sent = 0
				request = response.request
				return if Iodine.logger.nil? || request[:no_log]
				t_n = Time.now
				Iodine.log("#{request[:client_ip]} [#{t_n.utc}] \"#{request[:method]} #{request[:original_path]} #{request[:scheme]}\/#{request[:version]}\" #{response.status} #{response.bytes_written.to_s} #{((t_n - request[:time_recieved])*1000).round(2)}ms\n").clear
			end
		end
	end
end


