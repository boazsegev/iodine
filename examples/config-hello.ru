# frozen_string_literal: true

# A simple Hello World Rack application
module App
	HELLO_RESPONSE = [200, {"content-length" => 12}, ["Hello World!"]]
	def self.call(e)
		HELLO_RESPONSE
	end
end

run App
