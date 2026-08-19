# frozen_string_literal: true

module Iodine
  # The Iodine gem version number
  VERSION = "0.8.0.rc.02"
  # NeoRack supported extensions.
  def self.extensions
    return (@extensions ||= { iodine:   (VERSION.split(?.).map() {|i| i.to_i.to_s == i ? i.to_i : i }),
                              neo_rack: "0.0.2".split(?.).map(&:to_i),
                              cookies:  "0.0.1".split(?.).map(&:to_i),
                              from:     "0.0.1".split(?.).map(&:to_i),
                              pubsub:   "0.0.1".split(?.).map(&:to_i),
                              rack:     "1.3.0".split(?.).map(&:to_i),
                              rest:     "0.0.1".split(?.).map(&:to_i),
                              sse:      "0.0.1".split(?.).map(&:to_i),
                              ws:       "0.0.1".split(?.).map(&:to_i)
                            })
  end
end
