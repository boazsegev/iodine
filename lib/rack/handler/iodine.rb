require 'iodine'

class Iodine
	module Base
		# RackIO is the IO gateway for the HTTP request's body.
		#
		# creating a custom IO class allows Rack to directly access the C data and minimizes data duplication, enhancing performance.
		class RackIO
		end
		# This class inherits from {Iodine::Http Iodine::Http} and implements the methods required from a Rack handler.
		#
		# {Iodine::Rack Iodine::Rack} is an instance of this class.
		class RackHandler < Iodine::Http
			# returns the name of the handler.
			def name
				"Iodine::Rack"
			end
			# Runs a Rack app.
			def run(app, options = {})
				if(@app)
					old_app = @app
					@app = Proc.new do |env|
						ret = old_app.call(env)
						ret = app.call(env) if (!ret.is_a?(Array) || (ret.is_a?(Array) && ret[0].to_i == 404))
						ret
					end
				else
					@app = app
				end
				@port = options[:Port].to_i if options[:Port]
				@threads ||= ENV['MAX_THREADS'].to_i if ENV['MAX_THREADS']
				@on_http = @app
        start
				true
			end
		end
	end
	# Iodine::Rack is an instance of the Iodine::Http server child class and it will run a Rack Http server if called using
	# `Iodine::Rack.run app` or when called upon by Rack.
	Rack = Iodine::Base::RackHandler.new
end

ENV["RACK_HANDLER"] = 'iodine'

# make Iodine the default fallback position for Rack.
begin
	require 'rack/handler'
	Rack::Handler::WEBrick = Rack::Handler.get(:iodine)
rescue Exception

end

begin
	::Rack::Handler.register( 'iodine', 'Iodine::Rack') if defined?(::Rack)
rescue Exception

end
