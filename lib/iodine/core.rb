module Iodine
	public

	#######################
	## Events

	# Accepts a block and runs it asynchronously. This method runs asynchronously and returns immediately.
	#
	# use:
	#
	#      Iodine.run(arg1, arg2, arg3 ...) { |arg1, arg2, arg3...| do_something }
	#
	# the block will be run within the current context, allowing access to current methods and variables.
	#
	# @return [Iodine] always returns the reactor object.
	def run *args, &block
		@queue << [block, args]
		self
	end
	alias :run_async :run

	# @return [Iodine] Adds a shutdown tasks. These tasks should be executed in order of creation.
	def on_shutdown *args, &block
		@shutdown_queue << [block, args]
		self
	end

	# @return [nil] Signals Iodine to exit if it was running on Server or Timer mode. Tasks will rundown pending timeout.
	def signal_exit
		Process.kill("INT", 0) unless @stop
		nil
	end

	protected

	@queue = Queue.new
	@shutdown_queue = Queue.new
	@stop = true
	@done = false
	@logger = Logger.new(STDOUT)
	@spawn_count = @thread_count = 1
	@ios = {}
	@io_in = Queue.new
	@io_out = Queue.new
	@shutdown_mutex = Mutex.new
	@servers = {}


	def cycle
		work until @stop && @queue.empty?
		@shutdown_mutex.synchronize { shutdown }
		work until @queue.empty?
		run { true }
	end

	def work
		job = @queue && @queue.pop
		if job && job[0]
			begin
				job[0].call *job[1]
			rescue => e
				error e
			end
		end
	end

	Kernel.at_exit do
		threads = []
		@thread_count.times { threads << Thread.new {  cycle } }
		unless @stop
			catch(:stop) { sleep }
			@logger << "\nShutting down Iodine. Setting shutdown timeout to 25 seconds.\n"
			@stop = true
			# setup exit timeout.
			threads.each {|t| Thread.new {sleep 25; t.kill; t.kill } }
		end
		threads.each {|t| t.join rescue true }
	end

	# performed once - the shutdown sequence.
	def shutdown
		return if @done
		@stop = @done = true
		arr = []
		arr.push @shutdown_queue.pop until @shutdown_queue.empty?
		@queue.push arr.pop while arr[0]
		@thread_count.times {  run { true } }
	end

end
