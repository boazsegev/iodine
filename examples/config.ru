# A simple Rack application that prints out the `env` variable to the browser.
module App
	def self.call(env)
		txt = []
		if env['rack.upgrade?']
			env['rack.upgrade'] = self
		else
			env.each {|k,v| txt << "#{k}: #{v}\r\n" }
		end
		[200, {}, txt]
	end
	def self.on_open(e)
		e.subscribe :broadcast
	end
	def self.on_message(e, m)
		Iodine.publish :broadcast, m
	end
end

run App
