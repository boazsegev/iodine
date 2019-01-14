require 'iodine' unless defined?(::Iodine::VERSION)

module Iodine
  # Iodine's {Iodine::Rack} module provides a Rack complient interface (connecting Iodine to Rack) for an HTTP and Websocket Server.
  module Rack

    # Runs a Rack app, as par the Rack handler requirements.
    def self.run(app, options = {})
      # nested applications... is that a thing?
      Iodine.listen(service: :http, handler: app, port: options[:Port], address: options[:Address])

      # start Iodine
      Iodine.start

      true
    end
    IODINE_RACK_LOADED = true
  end
end

ENV['RACK_HANDLER'] = 'iodine'

# make Iodine the default fallback position for Rack.
begin
  require 'rack/handler' unless defined?(Rack::Handler)
  Rack::Handler::WEBrick = ::Iodine::Rack # Rack::Handler.get(:iodine)
rescue LoadError
end

begin
  ::Rack::Handler.register('iodine', 'Iodine::Rack') if defined?(::Rack::Handler)
  ::Rack::Handler.register('Iodine', 'Iodine::Rack') if defined?(::Rack::Handler)
rescue StandardError
end
