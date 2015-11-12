module Iodine

	# This is a mini-protocol used only to implement the SSL Handshake in a non-blocking manner,
	# allowing for a hardcoded timeout (which you can monkey patch) of 3 seconds.
	class SSLConnector < Protocol
		def initialize io, protocol, options = nil
			@protocol = protocol
			@options = options
			super(io)		
		end
		TIMEOUT = 3 # hardcoded SSL/TLS handshake timeout
		def on_open
			set_timeout TIMEOUT
			@ssl_socket = ::OpenSSL::SSL::SSLSocket.new(@io, ::Iodine.ssl_context)
			@ssl_socket.sync_close = true
		end

		# atempt an SSL Handshale
		def call
			return if @locker.locked?
			return unless @locker.try_lock
			begin
				@ssl_socket.accept_nonblock
			rescue ::IO::WaitReadable, ::IO::WaitWritable
				return
			rescue ::OpenSSL::SSL::SSLError
				@e = ::OpenSSL::SSL::SSLError.new "Self-signed Certificate?".freeze
				close
				return
			rescue => e
				::Iodine.warn "SSL Handshake failed with: #{e.message}".freeze
				@e = e
				close
				return
			ensure
				@locker.unlock
			end
			( (@ssl_socket.npn_protocol && ::Iodine.ssl_protocols[@ssl_socket.npn_protocol]) || @protocol).new @ssl_socket, @options
		end
		def on_close
			# inform
			::Iodine.warn "SSL Handshake #{@e ? "failed with: #{@e.message} (#{@e.class.name})" : 'timed-out.'}".freeze
			# the core @io is already closed, but let's make sure the SSL object is closed as well.
			@ssl_socket.close unless @ssl_socket.closed?
		end
	end

end
