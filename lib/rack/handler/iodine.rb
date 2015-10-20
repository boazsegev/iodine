require 'iodine/http'

::Rack::Handler.register( 'iodine', 'Iodine::Http::Rack') if defined?(::Rack)
