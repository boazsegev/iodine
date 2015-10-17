module Iodine

	# This class inherits from Protocol, but it includes some adjustments related to SSL connection handling.
	class SSLProtocol < Protocol

		# This is a mini-protocol used only to implement the SSL Handshake in a non-blocking manner,
		# allowing for a hardcoded timeout (which you can monkey patch) of 3 seconds.
		class SSLWait < Protocol
			TIMEOUT = 3 # hardcoded SSL/TLS handshake timeout
			def on_open
				timeout = TIMEOUT
				@ssl_socket = OpenSSL::SSL::SSLSocket.new(@io, Iodine.ssl_context)
				@ssl_socket.sync_close = true
			end

			# atempt an SSL Handshale
			def call
				return if @locker.locked?
				return unless @locker.try_lock
				begin
					@ssl_socket.accept_nonblock
					@locker.unlock
				rescue IO::WaitReadable, IO::WaitWritable
					@locker.unlock
					return
				rescue OpenSSL::SSL::SSLError
					@e = Exception.new "Self-signed Certificate?".freeze
					@locker.unlock
					return
				rescue => e
					Iodine.warn "SSL Handshake failed with: #{e.message}".freeze
					@e = e
					close
					@locker.unlock
					return
				end
				Iodine.protocol.new @ssl_socket
			end
			def on_close
				# inform
				Iodine.warn "SSL Handshake #{@e ? "failed with: #{@e.message} (#{@e.class.name})" : 'timed-out.'}".freeze
				# the core @io is already closed, but let's make sure the SSL object is closed as well.
				@ssl_socket.close unless @ssl_socket.closed?
			end
		end


		attr_reader :ssl_socket



		# reads from the IO up to the specified number of bytes (defaults to ~1Mb).
		def read size = 1048576
			@send_locker.synchronize do
				data = ''
				begin
					 (data << @ssl_socket.read_nonblock(size).to_s) until data.bytesize >= size
				rescue OpenSSL::SSL::SSLErrorWaitReadable, IO::WaitReadable, IO::WaitWritable

				rescue IOError
					close
				rescue => e
					Iodine.warn "SSL Protocol read error: #{e.class.name} #{e.message} (closing connection)"
					close
				end
				return false if data.to_s.empty?
				touch
				data
			end
		end

		def write data
			begin
				@send_locker.synchronize do
					r = @ssl_socket.write data
					touch
					r
				end				
			rescue => e
				close
				false
			end
		end
		alias :send :write
		alias :<< :write

		def on_close
			@ssl_socket.close unless @ssl_socket.closed?
			super
		end





		# This method initializes the SSL Protocol
		def initialize ssl_socket
			@ssl_socket = ssl_socket
			super(ssl_socket.io)
		end

		def self.accept io
			SSLWait.new(io)
		end


	end
end
