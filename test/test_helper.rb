$LOAD_PATH.unshift File.expand_path('../../lib', __FILE__)

require 'iodine'
require 'iodine/http'
Iodine.protocol = nil

require 'minitest/autorun'
