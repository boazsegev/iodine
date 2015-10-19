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

	@port = (ARGV.index('-p') && ARGV[ARGV.index('-p') + 1]) || ENV['PORT'] || 3000
	@bind = (ARGV.index('-ip') && ARGV[ARGV.index('-ip') + 1]) || ENV['IP'] || "0.0.0.0"
	@ssl = (ARGV.index('ssl') && true) || (@port == 443)
	@protocol = nil
	@ssl_context = nil
	@ssl_protocols = {}
	@time = Time.now

	@timeout_proc = Proc.new {|prot| prot.timeout?(@time) }
	@status_loop = Proc.new {|io|  @io_out << io if io.closed? || !(io.stat.readable? rescue false) }
	@close_callback = Proc.new {|prot| prot.on_close if prot }
	REACTOR = [ (Proc.new do
		if @queue.empty?
			#clear any closed IO objects.
			@time = Time.now
			@ios.keys.each &@status_loop
			@ios.values.each &@timeout_proc
			until @io_in.empty?
				n_io = @io_in.pop
				@ios[n_io[0]] = n_io[1]
			end
			until @io_out.empty?
				o_io = @io_out.pop
				o_io.close unless o_io.closed?
				run @ios.delete(o_io), &@close_callback
			end
			# react to IO events
			begin
				r = IO.select(@ios.keys, nil, nil, 0.15)
				r[0].each {|io| @queue << [@ios[io]] } if r
			rescue => e
				
			end
			unless @stop && @queue.empty?
				# @ios.values.each &@timeout_loop
				@check_timers && (@queue << @check_timers)
				@queue << REACTOR
			end
		else
			@queue << REACTOR
		end
	end )]
	# internal helper methods and classes.
	module Base
		# the server listener Protocol.
		class Listener < ::Iodine::Protocol
			def on_open
				@protocol = Iodine.protocol
				@ssl = Iodine.ssl
			end
			def call
				begin
					n_io = nil
					loop do
						n_io = @io.accept_nonblock
						@protocol.accept(n_io, @ssl)
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

	########
	## remember to set traps (once) when 'listen' is called.
	run do
		next unless @protocol
		if @protocol.is_a?( ::Class ) && @protocol.ancestors.include?( ::Iodine::Protocol )
			begin
				@server = ::TCPServer.new(@bind, @port)
			rescue => e
				Iodine.fatal e.message
				Iodine.fatal "Running existing tasks and exiting."
				@stop = true
				next
			end
			shut_down_proc = Proc.new {|protocol| protocol.on_shutdown ; protocol.close }
			on_shutdown do
				@logger << "Stopping to listen on port #{@port} and shutting down.\n"
				@server.close unless @server.closed?
				@ios.values.each {|p| run p, &shut_down_proc }
			end
			::Iodine::Base::Listener.accept(@server, false)
			@logger << "Iodine #{VERSION} is listening on port #{@port}#{ ' to SSL/TLS connections.' if @ssl}\n"
			if @spawn_count && @spawn_count.to_i > 1 && Process.respond_to?(:fork)
				@logger << "Server will run using #{@spawn_count.to_i} processes - Spawning #{@spawn_count.to_i - 1 } more processes.\n"
				(@spawn_count.to_i - 1).times do
					Process.fork do
						@logger << "Spawned process: #{Process.pid}.\n"
						on_shutdown { @logger << "Shutting down process #{Process.pid}.\n" }
						threads = []
						@queue.clear
						@queue << REACTOR
						@thread_count.times { threads << Thread.new {  cycle } }
						unless @stop
							old_int_trap = trap('INT') { throw :stop; trap('INT', old_int_trap) if old_int_trap }
							old_term_trap = trap('TERM') { throw :stop; trap('TERM', old_term_trap) if old_term_trap }
							catch(:stop) { sleep }
							@stop = true
							# setup exit timeout.
							threads.each {|t| Thread.new {sleep 25; t.kill; t.kill } }
						end
						threads.each {|t| t.join rescue true }
					end
				end
				
			end
			@logger << "Press ^C to stop the server.\n"
		else
			@logger << "Iodine #{VERSION} is running.\n"
			on_shutdown do
				@logger << "Iodine says goodbye.\n"
			end
			@logger << "Press ^C to stop the cycling.\n"			
		end
		old_int_trap = trap('INT') { throw :stop; trap('INT', old_int_trap) if old_int_trap }
		old_term_trap = trap('TERM') { throw :stop; trap('TERM', old_term_trap) if old_term_trap }
		@queue << REACTOR
	end
end
