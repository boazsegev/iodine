# This example uses Rack::ETag to allow for response caching.

require 'rack'
require 'iodine'

App = Proc.new do |env|
   [200,
     {   "Content-Type" => "text/html".freeze,
         "Content-Length" => "16".freeze },
     ['Hello from Rack!'.freeze]  ]
end

use Rack::ConditionalGet
use Rack::ETag, 'public'

run App
