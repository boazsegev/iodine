# frozen_string_literal: true

module Iodine
  # The Iodine gem version number
  VERSION = "0.8.0.dev"
  # NeoRack supported extensions.
  def self.extensions
    return (@extentions ||= { neo_rack: "0.0.2".split(?.),
                              ws: "0.0.1".split(?.),
                              sse: "0.0.1".split(?.),
                              pubsub: "0.0.1".split(?.),
                              cookies: "0.0.1".split(?.),
                              from: "0.0.1".split(?.)
                            })
  end
end
