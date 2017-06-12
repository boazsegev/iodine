require 'iodine'

module Iodine
   # The {Iodine::HTTP} server module powers the default {Iodine::Rack} as well as
   # allows multiple HTTP servers and applications to share an Iodine process
   # cluster and it's pub/sub engine (sharing channels etc').
   #
   # Normally, only the {Iodine::Rack} interface will be utilized. This module
   # was exposed for edge cases and in the hopes of future TLS/SSL support.
  module HTTP
  end
end
