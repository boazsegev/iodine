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

	# forces Iodine to start prematurely and asyncronously. This might cause Iodine's exit sequence to end abruptly, depending how the hosting application behaves.
	#
	# calling this method repeatedly will be ignored unless Iodine's threads have all died.
	#
	# @return [Iodine] the method will allways return `self` (Iodine).
	def force_start!
		return self if @force_running
		@force_running = true
		thread = Thread.new do
			startup true
			@force_running = false
			initialize_tasks
			@stop = false if @protocol
		end
		Kernel.at_exit {thread.raise("stop"); thread.join}
		self
	end

	protected

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
				job[0].call(*job[1])
			rescue => e
				error e
			end
		end
	end

	def startup use_rescue = false, hide_message = false
		@force_running = true
		threads = []
		(@thread_count ||= 1).times { threads << Thread.new { Thread.current[:buffer] ||= String.new; Thread.current[:write_buffer] ||= String.new; cycle } }
		unless @stop
			if use_rescue
				sleep rescue true
			else
				old_int_trap = trap('INT') { throw :stop; old_int_trap.respond_to?(:call) && old_int_trap.call }
				old_term_trap = trap('TERM') { throw :stop; old_term_trap.respond_to?(:call) && old_term_trap.call }
				catch(:stop) { sleep }
			end
			log "\nShutting down #{self == Iodine ? 'Iodine' : "#{self.name} (Iodine)"}. Setting shutdown timeout to 25 seconds.\n" unless hide_message
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
