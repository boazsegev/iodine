require 'iodine/http'

::Rack::Handler.register( 'iodine', 'Iodine::Base::Rack') if defined?(::Rack)
