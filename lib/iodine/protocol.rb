module Iodine

	# This is the Basic Iodine server unit - a network protocol.
	#
	# A new protocol instance will be created for every network connection.
	#
	# The recommended use is to inherit this class (or {SSLProtocol}) and override any of the following:
	# on_open:: called whenever the Protocol is initialized. Override this to initialize the Protocol object.
	# on_message(data):: called whenever data is received from the IO. Override this to implement the actual network protocol.
	# on_close:: called AFTER the Protocol's IO is closed.
	# on_shutdown:: called when the server's shutdown process had started and BEFORE the Protocol's IO is closed. It allows graceful shutdown for network protocols.
	# ping::
	#
	# Once the network protocol class was created, remember to tell Iodine about it:
	#       class MyProtocol << Iodine::Protocol
	#           # your code here
	#       end
	#       # tell Iodine
	#       Iodine.protocol = MyProtocol
	#
	class Protocol

		# returns the raw IO object. Using one of the Protocol methods {#write}, {#read}, {#close} is prefered over direct access.
		attr_reader :io

		# Sets the timeout in seconds for IO activity (set timeout within {#on_open}).
		#
		# After timeout is reached, {#ping} will be closed. The connection will be closed if {#ping} returns `false` or `nil`.
		def set_timeout seconds
			@timeout = seconds
		end

		# This method is called whenever the Protocol is initialized - i.e.:
		# a new connection is established or an old connection switches to this protocol.
		def on_open
		end
		# This method is called whenever data is received from the IO.
		def on_message data
		end

		# This method is called AFTER the Protocol's IO is closed - it will only be called once.
		def on_close
		end

		# This method is called when the server's shutdown process had started and BEFORE the Protocol's IO is closed. It allows graceful shutdown for network protocols.
		def on_shutdown
		end

		# This method is called whenever a timeout has occurred. Either implement a ping or return `false` to disconnect.
		#
		# A `false` or `nil` return value will cause disconnection
		def ping
			false
		end


		# Closes the IO object.
		# @return [nil]
		def close
			@io.close unless @io.closed?
			nil
		end
		alias :disconnect :close

		# reads from the IO up to the specified number of bytes (defaults to ~2Mb).
		def read size = 2_097_152
			touch
			@io.recv_nonblock( size  )
		rescue => e
			nil
		end

		# this method, writes data to the socket / io object.
		def write data
			begin
				@send_locker.synchronize do
					r = @io.write data
					touch
					r
				end
			rescue => e
				# GReactor.warn e
				close
			end
		end


		# This method allows switiching the IO's protocol that will be used the NEXT time
		# Iodine receives data using this protocol's IO.
		#
		# Switing protocols bypasses the {#on_close} method. Override this method for any cleanup needed (if at any),
		# but remember to call `super` for the actual protocol switching implementation.
		def switch_protocol new_protocol
			Iodine.switch_protocol @io, new_protocol
		end

		#################
		## the following are Iodine's "system" methods, used internally. Don't override.


		# This method is used by Iodine to initialized the Protocol.
		#
		# Normally you won't need to override this method. Override {#on_open} instead.
		def initialize io
			@timeout ||= nil
			@send_locker = Mutex.new
			@locker = Mutex.new
			@io = io
			switch_protocol self
			touch
			on_open
		end

		# Called by Iodine whenever there is data in the IO's read buffer.
		#
		# Normally you won't need to override this method. Override {#on_message} instead.
		def call
			return unless @locker.try_lock
			begin
				data = read
				if data
					on_message(data)
					data.clear
				end
			ensure
				@locker.unlock
			end
		end


		# This method is used by Iodine to ask whether a timeout has occured.
		#
		# Normally you won't need to override this method. See {#ping}
		def timeout? time
			(ping || close) if @timeout && !@send_locker.locked? && ( (time - @last_active) > @timeout )
		end



		# This method is used by Iodine to create the IO handler whenever a new connection is established.
		#
		# Normally you won't need to override this method.
		def self.accept io
			self.new(io)
		end

		protected

		# This methos updates the timeout "watch", signifying the IO was active.
		def touch
			@last_active = Iodine.time
		end
	end

end
