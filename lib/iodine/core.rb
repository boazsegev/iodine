module Iodine
	public

	#######################
	## Events

	# Accepts a block and runs it asynchronously. This method runs asynchronously and returns immediately.
	#
	# use:
	#
	#      GReactor.run_async(arg1, arg2, arg3 ...) { |arg1, arg2, arg3...| do_something }
	#
	# the block will be run within the current context, allowing access to current methods and variables.
	#
	# @return [GReactor] always returns the reactor object.
	def run *args, &block
		queue block, args
	end
	alias :run_async :run

	# This method runs an object's method asynchronously and returns immediately. This method will also run an optional callback if a block is supplied.
	#
	# This method accepts:
	# object:: an object who's method will be called.
	# method:: the method's name to be called. type: Symbol.
	# *args:: any arguments to be passed to the method.
	# block (optional):: If a block is supplied, it will be used as a callback and the method's return value will be passed on to the block.
	#
	# @return [GReactor] always returns the reactor object.
	def callback object, method_name, *args, &block
		block ? queue(@callback_proc, [object.method(method_name), args, block]) : queue(object.method(method_name), args)
	end

	# Adds a job OR a block to the queue. {GReactor.run_async} and {GReactor.callback} extend this core method.
	#
	# This method accepts two possible arguments:
	# job:: An object that answers to `call`, usually a Proc or Lambda.
	# args:: (optional) An Array of arguments to be passed on to the executed method.
	#
	# @return [GReactor] always returns the reactor object.
	#
	# The callback will NOT be called if the executed job failed (raised an exception).
	# @see .run_async
	#
	# @see .callback
	def queue job, args = nil
		@queue << [job, args]
		self
	end

	# Adds a shutdown tasks. These tasks should be executed in order of creation.
	def on_shutdown *args, &block
		@shutdown_queue << [block, args]
		self
	end

	protected

	@queue = Queue.new
	@shutdown_queue = Queue.new
	@stop = true
	@done = false
	@logger = Logger.new(STDOUT)
	@thread_count = 1
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

	def shutdown
		return if @done
		@stop = @done = true
		arr = []
		arr.push @shutdown_queue.pop until @shutdown_queue.empty?
		@queue.push arr.pop while arr[0]
		@thread_count.times {  run { true } }
	end

end
