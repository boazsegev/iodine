module Iodine

	public

	# @return [Time] Gets the last time at which the IO Reactor was last active (last "tick").
	def time
		@time
	end
	# Replaces (or creates) an IO's protocol object.
	#
	# Accepts 2 arguments, in the following order:
	#
	# io:: the raw IO object.
	# protocol:: a protocol instance - should be an instance of a class inheriting from {Iodine::Protocol}. type will NOT be checked - but Iodine could break if there is a type mismatch.
	# @return [Protocol]
	def switch_protocol *args
		@io_in << args
		args[1]
	end

	# @return [Array] Returns an Array with all the currently active connection's Protocol instances.
	def to_a
		@ios.values
	end


	protected

	# internal helper methods and classes.
	module Base
		# the server listener Protocol.
		class Listener < ::Iodine::Protocol
			def on_open
				@protocol = Iodine.protocol
				@ssl = Iodine.ssl
				@accept_proc = @protocol.method(:accept)
			end
			def call
				begin
					n_io = nil
					loop do
						n_io = @io.accept_nonblock
						# @protocol.accept(n_io, @ssl)
						Iodine.run n_io, @ssl, &(@accept_proc)
					end
				rescue Errno::EWOULDBLOCK => e

				rescue => e
					n_io.close if n_io && !n_io.closed?
					@stop = true
					raise e
				end
			end
		end
	end
end
