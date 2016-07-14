require 'iodine'
require 'iodine/iodine'

module Iodine
  module Base
    # RackIO is the IO gateway for the HTTP request's body.
    #
    # creating a custom IO class allows Rack to directly access the C data and minimizes data duplication, enhancing performance.
    class RackIO
    end
  end
  # {Iodine::Rack} is an Iodine HTTP and Websocket Rack server bundled with {Iodine} for your convinience.
  module Rack
    # get/set the Rack application.
    def self.app=(val)
      @app = val
    end

    # get/set the Rack application.
    def self.app
      @app
    end

    # get/set the HTTP connection timeout property. Defaults to 5. Limited to a maximum of 255. 0 values are silently ignored.
    def self.timeout=(t)
      @timeout = t
    end

    # get/set the HTTP connection timeout property. Defaults to 5.
    def self.timeout
      @timeout
    end

    # get/set the HTTP public folder property. Defaults to 5. Defaults to the incoming argumrnts or `nil`.
    def self.public=(val)
      @public = val
    end
    @public_folder = ARGV[ARGV.index('-www') + 1] if ARGV.index('-www')

    # get/set the HTTP public folder property. Defaults to 5.
    def self.public
      @public
    end

    # get/set the maximum HTTP body size for incoming data. Defaults to ~50Mb. 0 values are silently ignored.
    def self.max_body_size=(val)
      @max_body_size = val
    end

    # get/set the maximum HTTP body size for incoming data. Defaults to ~50Mb.
    def self.max_body_size
      @max_body_size
    end

    # get/set the HTTP logging value (true / false). Defaults to the incoming argumrnts or `false`.
    def self.log=(val)
      @log = val
    end

    # get/set the HTTP logging value (true / false). Defaults to the incoming argumrnts or `false`.
    def self.log
      @log
    end
    @log = true if ARGV.index('-v')

    # get/set the HTTP listenning port. Defaults to 3000.
    def self.port=(val)
      @port = val
    end

    # get/set the HTTP listenning port. Defaults to 3000.
    def self.port
      @port
    end
    @port = ARGV[ARGV.index('-p') + 1] if ARGV.index('-p')
    @port ||= 3000

    # get/set the HTTP socket binding address. Defaults to `nil` (usually best).
    def self.address=(val)
      @address = val
    end

    # get/set the HTTP socket binding address. Defaults to `nil` (usually best).
    def self.address
      @address
    end
    @address = nil

    # Runs a Rack app, as par the Rack handler requirements.
    def self.run(app, _options = {})
      @app
      if @app && @app != app
        old_app = @app
        @app = proc do |env|
          ret = old_app.call(env)
          ret = app.call(env) if !ret.is_a?(Array) || (ret.is_a?(Array) && ret[0].to_i == 404)
          ret
        end
      else
        @app = app
      end
      Iodine.start
      true
    end
  end
end

ENV['RACK_HANDLER'] = 'iodine'

# make Iodine the default fallback position for Rack.
begin
  require 'rack/handler'
  Rack::Handler::WEBrick = Rack::Handler.get(:iodine)
rescue Exception

end

begin
  ::Rack::Handler.register('iodine', 'Iodine::Rack') if defined?(::Rack)
rescue Exception

end
