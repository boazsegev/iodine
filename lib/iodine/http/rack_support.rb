module Iodine

	module Base
		# This (will be) a Rack handler for the Iodine HTTP server.
		module Rack
			module_function
			def run(app, options = {})
				@app = app

				Iodine.protocol ||= Iodine::HTTP
				@pre_rack_handler = Iodine.protocol
				Iodine.protocol.http_app = self
				true
			end
			def call request, response
				if @pre_rack_handler
					tmp = @pre_rack_handler.call(request, response)
					return tmp if tmp
				end
				# response.quite!
				res = @app.call rack_env(request)
				raise "Rack app returned an unexpected value: #{res.to_s}" unless res && res.is_a?(Array)
				response.status = res[0]
				response.headers.clear
				response.headers.update res[1]
				response.body = res[2]
				response.raw_cookies.clear
				response.headers['Set-Cookie'] = response.headers.delete('Set-Cookie').split("\n").join("\r\nSet-Cookie: ") if response.headers['Set-Cookie']
				response.request[:no_log] = true
				true
			end

			protected


			def self.rack_env request
				env = RACK_DICTIONARY.dup
				# env['pl.request'] = @request
				# env.each {|k, v| env[k] = @request[v] if v.is_a?(Symbol)}
				RACK_ADDON.each {|k, v| env[k] = (request[v].is_a?(String) ? ( request[v].frozen? ? request[v].dup.force_encoding('ASCII-8BIT') : request[v].force_encoding('ASCII-8BIT') ): request[v])}
				request.each {|k, v| env["HTTP_#{k.upcase.tr('-', '_')}"] = v if k.is_a?(String) }
				env['rack.input'.freeze] ||= StringIO.new(''.force_encoding('ASCII-8BIT'.freeze))
				env['CONTENT_LENGTH'.freeze] = env.delete 'HTTP_CONTENT_LENGTH'.freeze if env['HTTP_CONTENT_LENGTH'.freeze]
				env['CONTENT_TYPE'.freeze] = env.delete 'HTTP_CONTENT_TYPE'.freeze if env['HTTP_CONTENT_TYPE'.freeze]
				env['HTTP_VERSION'.freeze] = "HTTP/#{request[:version].to_s}"
				env['QUERY_STRING'.freeze] ||= ''
				env['rack.errors'.freeze] = StringIO.new('')
				env
			end

			RACK_ADDON = {
				'PATH_INFO'			=> :original_path,
				'REQUEST_PATH'		=> :path,
				'QUERY_STRING'		=> :quary_params,
				'SERVER_NAME'		=> :host_name,
				'REQUEST_URI'		=> :query,
				'SERVER_PORT'		=> :port,
				'REMOTE_ADDR'		=> :client_ip,
				# 'gr.params'			=> :params,
				# 'gr.cookies'		=> :cookies,
				'REQUEST_METHOD'	=> :method,
				'rack.url_scheme'	=> :scheme,
				'rack.input'		=> :rack_input
			}

			RACK_DICTIONARY = {
				"GATEWAY_INTERFACE"	=>"CGI/1.2",
				'SERVER_SOFTWARE'	=> "Iodine v. #{Iodine::VERSION}",
				'SCRIPT_NAME'		=> ''.force_encoding('ASCII-8BIT'),
				'rack.logger'		=> Iodine,
				'rack.multithread'	=> true,
				'rack.multiprocess'	=> false,
				# 'rack.hijack?'		=> false,
				# 'rack.hijack_io'	=> nil,
				'rack.run_once'		=> false
			}
			RACK_DICTIONARY['rack.version'] = ::Rack.version.split('.') if defined?(::Rack)
			HASH_SYM_PROC = Proc.new {|h,k| k = (Symbol === k ? k.to_s : k.to_s.to_sym); h.has_key?(k) ? h[k] : (h["gr.#{k.to_s}"] if h.has_key?("gr.#{k.to_s}") ) }
		end
	end
end

# ENV["RACK_HANDLER"] = 'grhttp'

# make Iodine the default fallback position for Rack.
begin
	require 'rack/handler'
	Rack::Handler::WEBrick = Rack::Handler.get(:grhttp)
rescue Exception => e

end
::Rack::Handler.register( 'grhttp', 'Iodine::Base::Rack') if defined?(::Rack)

######
## example requests

# GET /stream HTTP/1.1
# Host: localhost:3000
# Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8
# Cookie: user_token=2INa32_vDgx8Aa1qe43oILELpSdIe9xwmT8GTWjkS-w
# User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10) AppleWebKit/600.1.25 (KHTML, like Gecko) Version/8.0 Safari/600.1.25
# Accept-Language: en-us
# Accept-Encoding: gzip, deflate
# Connection: keep-alive