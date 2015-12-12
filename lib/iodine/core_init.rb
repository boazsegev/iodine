module Iodine
	protected

	def extended base
		base.instance_exec do
			initialize_core
			initialize_io
			initialize_timers
			initialize_tasks
		end
		base.protocol = :cycle unless base == Iodine
	end

	def initialize_core
		# initializes all the core components
		# referenced in `core.rb`
		@queue = Queue.new
		@shutdown_queue = Queue.new
		@stop = true
		@done = false
		@logger = Logger.new(STDOUT)
		@spawn_count = 1
		@thread_count = nil
		@ios = {}
		@io_in = Queue.new
		@io_out = Queue.new
		@shutdown_mutex = Mutex.new
		@servers = {}
		Kernel.at_exit do
			if self == Iodine
				startup
			else
				Iodine.protocol ||= :cycle if @timers.any? || @protocol
				thread = Thread.new { startup true }
				Iodine.on_shutdown { thread.raise 'stop' ; thread.join }
			end
		end
	end

	def initialize_io
		# initializes all the IO components
		# referenced in `io.rb`
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
		@reactor = [ (Proc.new do
			@ios.keys.each(&@status_loop)
			@ios.values.each(&@timeout_proc)
			until @io_in.empty?
				n_io = @io_in.pop
				@ios[n_io[0]] = n_io[1]
			end
			until @io_out.empty?
				o_io = @io_out.pop
				o_io.close unless o_io.closed?
				run @ios.delete(o_io), &@close_callback
			end
			if @queue.empty?
				#clear any closed IO objects.
				@time = Time.now
				# react to IO events
				begin
					r = IO.select(@ios.keys, nil, nil, 0.15)
					r[0].each {|io| @queue << [@ios[io]] } if r
				rescue 
					
				end
				unless @stop && @queue.empty?
					# @ios.values.each &@timeout_loop
					@check_timers && (@queue << @check_timers)
					@queue << @reactor
				end
			else
				@queue << @reactor
			end
		end )]
	end

	def initialize_timers
		@timer_locker = Mutex.new
		@timers = []
		# cycles through timed jobs, executing and/or deleting them if their time has come.
		@check_timers = [(Proc.new do
			@timer_locker.synchronize { @timers.delete_if {|t| t.done? } }
		end)]
	end

	def initialize_tasks
		# initializes actions to be taken when starting to run
		run do
			@protocol ||= :cycle if @timers.any?
			next unless @protocol
			if @protocol.is_a?( ::Class ) && @protocol.ancestors.include?( ::Iodine::Protocol )
				begin
					@server = ::TCPServer.new(@bind, @port)
				rescue => e
					fatal e.message
					fatal "Running existing tasks with #{@thread_count} thread(s) and exiting."
					@queue << @reactor
					Process.kill("INT", 0)
					next
				end
				on_shutdown do
					@server.close unless @server.nil? || @server.closed?
					log "Stopped listening to port #{@port}.\n"
				end
				::Iodine::Base::Listener.accept(@server, false)
				log "Iodine #{VERSION} is listening on port #{@port}#{ ' (SSL/TLS)' if @ssl} with #{@thread_count} thread(s).\n"
				if @spawn_count && @spawn_count.to_i > 1 && Process.respond_to?(:fork)
					log "Server will run using #{@spawn_count.to_i} processes - Spawning #{@spawn_count.to_i - 1 } more processes.\n"
					(@spawn_count.to_i - 1).times do
						Process.fork do
							log "Spawned process: #{Process.pid}.\n"
							on_shutdown { log "Shutting down process #{Process.pid}.\n" }
							@queue.clear
							@queue << @reactor
							startup false, true
						end
					end
					
				end
				log "Press ^C to stop the server.\n"
			else
				log "#{self == Iodine ? 'Iodine' : "#{self.name} (Iodine)"} #{VERSION} is running with #{@thread_count} thread(s).\n"
				log "Press ^C to stop the cycling.\n"
			end
			on_shutdown do
				shut_down_proc = Proc.new {|prot| prot.on_shutdown ; prot.close }
				@ios.values.each {|p| run p, &shut_down_proc }
				@queue << @reactor
			end
			@queue << @reactor
		end
	end
end
