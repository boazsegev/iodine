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

	@port = ((ARGV.index('-p') && ARGV[ARGV.index('-p') + 1]) || ENV['PORT'] || 3000).to_i
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

	########
	## remember to set traps (once) when 'listen' is called.
	run do
		next unless @protocol
		if @protocol.is_a?( ::Class ) && @protocol.ancestors.include?( ::Iodine::Protocol )
			begin
				@server = ::TCPServer.new(@bind, @port)
			rescue => e
				fatal e.message
				fatal "Running existing tasks and exiting."
				Process.kill("INT", 0)
				next
			end
			shut_down_proc = Proc.new {|protocol| protocol.on_shutdown ; protocol.close }
			on_shutdown do
				log "Stopping to listen on port #{@port} and shutting down.\n"
				@server.close unless @server.closed?
				@ios.values.each {|p| run p, &shut_down_proc }
			end
			::Iodine::Base::Listener.accept(@server, false)
			log "Iodine #{VERSION} is listening on port #{@port}#{ ' to SSL/TLS connections.' if @ssl}\n"
			if @spawn_count && @spawn_count.to_i > 1 && Process.respond_to?(:fork)
				log "Server will run using #{@spawn_count.to_i} processes - Spawning #{@spawn_count.to_i - 1 } more processes.\n"
				(@spawn_count.to_i - 1).times do
					Process.fork do
						log "Spawned process: #{Process.pid}.\n"
						on_shutdown { log "Shutting down process #{Process.pid}.\n" }
						@queue.clear
						@queue << REACTOR
						startup false, true
					end
				end
				
			end
			log "Press ^C to stop the server.\n"
		else
			log "Iodine #{VERSION} is running.\n"
			log "Press ^C to stop the cycling.\n"
		end
		@queue << REACTOR
	end
end
