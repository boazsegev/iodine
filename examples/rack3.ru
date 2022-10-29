#!/usr/bin/env ruby
# frozen_string_literal: true
# adjusted from the code suggested by @taku0 (GitHub issue #131)
require 'rack'
require 'iodine'

run { |env|
  response = Rack::Response.new(nil, 204)
  response.set_cookie('aaa', 'aaa')
  response.set_cookie('bbb', 'bbb')
  response.finish
}
