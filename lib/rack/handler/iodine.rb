require 'iodine' unless defined?(::Iodine::VERSION)

module Iodine
  # Iodine's {Iodine::Rack} module provides a Rack compliant interface (connecting Iodine to Rack) for an HTTP and Websocket Server.
  module Rack

    # Runs a Rack app, as par the Rack handler requirements.
    def self.run(app, options = {})
      # nested applications... is that a thing?
      Iodine.listen(service: :http, handler: app, port: options[:Port], address: options[:Host])

      # start Iodine
      Iodine.start

      true
    end

    # patches an assumption by Rack, issue #98 code donated by @Shelvak (Néstor Coppi)
    def self.shutdown
      Iodine.stop
    end

    IODINE_RACK_LOADED = true
  end
end

ENV['RACK_HANDLER'] ||= 'iodine'

begin
  ::Rack::Handler.register('iodine', 'Iodine::Rack') if defined?(::Rack::Handler)
  ::Rack::Handler.register('Iodine', 'Iodine::Rack') if defined?(::Rack::Handler)
rescue StandardError
end
