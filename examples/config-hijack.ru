# A simple Rack application that prints out the `env` variable to the browser.
module EchoApp
	def self.call(e)
		io = e['rack.hijack'].call
		txt = ["Hijacked!\r\n"]
		e.each {|k,v| txt << "#{k}: #{v}\r\n" }
		txt = txt.join()
		Thread.new {sleep(0.5); io.write("HTTP/1.1 200 OK\r\nContent-Length: #{txt.bytesize}\r\n\r\n#{txt}"); io.close; puts("Sent"); }
		[200, {}, []]
	end
end

run EchoApp
