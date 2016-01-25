require 'iodine/http'

class Iodine
	class Http
		# This is the Rack handler for the Iodine's HTTP server.
		module Rack
			module_function
			def run(app, options = {})
				@app = app
        server = Iodine::Http.new
				server.threads ||= 18
				server.port = options[:Port] if options[:Port]
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
