require 'iodine' unless defined?(::Iodine::VERSION)

module Iodine
  # {Iodine::Rack} provides a Rack complient interface (connecting Iodine to Rack) for an HTTP and Websocket Server.
  #
  # {Iodine::Rack} also provides convinient access to the {Iodine::HTTP.listen} function, which powers the {Iodine::Rack} server.
  module Rack
    # get/set the Rack application.
    def self.app=(val)
      @app = val
    end

    # get/set the Rack application.
    def self.app
      @app
    end
    @app = nil

    # get/set the HTTP connection timeout property. Defaults to 5. Limited to a maximum of 255. 0 values are silently ignored.
    def self.timeout=(t)
      @timeout = t
    end

    # get/set the HTTP connection timeout property. Defaults to 5.
    def self.timeout
      @timeout
    end
    @timeout = 0

    # get/set the Websocket connection timeout property. Defaults to 40 seconds. Limited to a maximum of 255. 0 values are silently ignored.
    def self.ws_timeout=(t)
      @ws_timeout = t
    end

    # get/set the Websocket connection timeout property. Defaults to 40 seconds. Limited to a maximum of 255. 0 values are silently ignored.
    def self.ws_timeout
      @ws_timeout
    end
    @ws_timeout = 0

    # get/set the HTTP public folder property. Defaults to 5. Defaults to the incoming argumrnts or `nil`.
    def self.public=(val)
      @public = val
    end
    @public = nil
    @public = ARGV[ARGV.index('-www') + 1] if ARGV.index('-www')

    # get/set the HTTP public folder property. Defaults to 5.
    def self.public
      @public
    end

    # get/set the maximum HTTP body size for incoming data. Defaults to ~50Mb. 0 values are silently ignored.
    def self.max_body_size=(val)
      @max_body = val
    end

    # get/set the maximum HTTP body size for incoming data. Defaults to ~50Mb.
    def self.max_body_size
      @max_body
    end

    # get/set the maximum HTTP body size for incoming data. Defaults to ~50Mb. 0 values are silently ignored.
    def self.max_body=(val)
      @max_body = val
    end

    # get/set the maximum HTTP body size for incoming data. Defaults to ~50Mb.
    def self.max_body
      @max_body
    end
    @max_body = 0

    # get/set the maximum Websocket body size for incoming data. Defaults to defaults to ~250KB. 0 values are silently ignored.
    def self.max_msg_size=(val)
      @max_msg = val
    end

    # get/set the maximum Websocket body size for incoming data. Defaults to defaults to ~250KB. 0 values are silently ignored.
    def self.max_msg_size
      @max_msg
    end

    # get/set the maximum Websocket body size for incoming data. Defaults to defaults to ~250KB. 0 values are silently ignored.
    def self.max_msg=(val)
      @max_msg = val
    end

    # get/set the maximum Websocket body size for incoming data. Defaults to defaults to ~250KB. 0 values are silently ignored.
    def self.max_msg
      @max_msg
    end
    @max_msg = 0

    # get/set the HTTP logging value (true / false). Defaults to the incoming argumrnts or `false`.
    def self.log=(val)
      @log = val
    end

    # get/set the HTTP logging value (true / false). Defaults to the incoming argumrnts or `false`.
    def self.log
      @log
    end
    @log = false
    @log = true if ARGV.index('-v')
    @log = false if ARGV.index('-q')

    # get/set the HTTP listening port. Defaults to 3000.
    def self.port=(val)
      @port = val
    end

    # get/set the HTTP listening port. Defaults to 3000.
    def self.port
      @port
    end
    @port = ARGV[ARGV.index('-p') + 1] if ARGV.index('-p')
    @port ||= 3000.to_s

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
    def self.run(app, options = {})
      # nested applications... is that a thing?
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
      @port = options[:Port].to_s if options[:Port]
      @address = options[:Address].to_s if options[:Address]

      # # provide Websocket features using Rack::Websocket
      # Rack.send :remove_const, :Websocket if defined?(Rack::Websocket)
      # Rack.const_set :Websocket, ::Iodine::Websocket

      # start Iodine
      Iodine.start

      # # remove the Websocket features from Rack::Websocket
      # Rack.send :remove_const, :Websocket

      true
    end
    IODINE_RACK_LOADED = true
  end
end

# Iodine::Rack.app = proc { |env| p env; puts env['rack.input'].read(1024).tap { |s| puts "Got data #{s.length} long, #{s[0].ord}, #{s[1].ord} ... #{s[s.length - 2].ord}, #{s[s.length - 1].ord}:" if s }; env['rack.input'].rewind; [404, {}, []] }

ENV['RACK_HANDLER'] = 'iodine'

# make Iodine the default fallback position for Rack.
begin
  require 'rack/handler' unless defined?(Rack::Handler)
  Rack::Handler::WEBrick = ::Iodine::Rack # Rack::Handler.get(:iodine)
rescue Exception

end

begin
  ::Rack::Handler.register('iodine', 'Iodine::Rack') if defined?(::Rack::Handler)
rescue Exception

end
