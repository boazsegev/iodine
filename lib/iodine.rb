require 'logger'
require 'socket'
# require 'securerandom'


module Iodine
  extend self
end


require "iodine/version"
require "iodine/settings"
require "iodine/logging"
require "iodine/core"
require "iodine/timers"
require "iodine/protocol"
require "iodine/ssl_protocol"
require "iodine/io"
