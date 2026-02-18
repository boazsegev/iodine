# frozen_string_literal: true

module Iodine
  # The Iodine gem version number
  VERSION = "0.8.0.rc.02"
  # NeoRack supported extensions.
  def self.extensions
    return (@extensions ||= { neo_rack: "0.0.2".split(?.),
                              cookies: "0.0.1".split(?.),
                              from: "0.0.1".split(?.),
                              pubsub: "0.0.1".split(?.),
                              rack: "1.3.0".split(?.),
                              rest: "0.0.1".split(?.),
                              sse: "0.0.1".split(?.),
                              ws: "0.0.1".split(?.)
                            })
  end
end
