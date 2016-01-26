require 'iodine/http'

class Iodine
	class Http
		# This is the Rack handler for the Iodine's HTTP server.
		module Rack
			module_function
			@threads = 8
			def threads= t_count
				@threads = t_count
			end
			def threads
				@threads
			end

			def run(app, options = {})
        puts "press E to start"
        gets
				@app = app
        server = Iodine::Http.new
				server.threads = @threads
				server.port = options[:Port].to_i if options[:Port]
				server.on_request = @app
        server.start
				true
			end
		end
	end
end

# ENV["RACK_HANDLER"] = 'iodine'

# make Iodine the default fallback position for Rack.
begin
	require 'rack/handler'
	Rack::Handler::WEBrick = Rack::Handler.get(:iodine)
rescue Exception

end
::Rack::Handler.register( 'iodine', 'Iodine::Http::Rack') if defined?(::Rack)
