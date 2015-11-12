module Iodine

	#######################
	## Timers


	# Every timed event is a member of the TimedEvent class and responds to it's methods. 
	class TimedEvent

		# Sets/gets how often a timed event repeats, in seconds.
		attr_accessor :interval
		# Sets/gets how many times a timed event repeats.
		# If set to false or -1, the timed event will repead until the application quits.
		attr_accessor :repeat_limit
		# Allows you to acess or change the timer's Proc object.
		attr_accessor :job

		# Initialize a timed event.
		def initialize reactor, interval, repeat_limit = -1, args=[], job=nil
			@reactor = reactor
			@interval = interval
			@repeat_limit = repeat_limit ? repeat_limit.to_i : -1
			@job = job || (Proc.new { stop! })
			@next = @reactor.time + interval
			args << self
			@args = args
		end

		# stops a timed event.
		# @return [Iodine::TimedEvent] returns the TimedEvent object.
		def stop!
			@repeat_limit = 0
			@next = @reactor.time
			self
		end

		# Returns true if the timer is finished.
		#
		# If the timed event is due, this method will also add the event to the queue.
		# @return [true, false]
		def done?
			return false unless @next <= @reactor.time
			return true if @repeat_limit == 0
			@repeat_limit -= 1 if @repeat_limit.to_i > 0
			@reactor.run(*@args, &@job)
			@next = @reactor.time + @interval
			@repeat_limit == 0
		end
	end

	public

	# pushes a timed event to the timers's stack
	#
	# accepts:
	# seconds:: the minimal amount of seconds to wait before calling the handler's `call` method.
	# *arg:: any arguments that will be passed to the handler's `call` method.
	# &block:: the block to execute.
	#
	# A block is required.
	#
	# On top of the arguments passed to the `run_after` method, the timer object will be passed as the last agrument to the receiving block.
	#
	# Timed event's time of execution is dependant on the workload and continuous uptime of the process (timed events AREN'T persistent).
	#
	# @return [Iodine::TimedEvent] returns the new TimedEvent object.
	def run_after seconds, *args, &block
		timed_job seconds, 1, args, block
	end

	# pushes a repeated timed event to the timers's stack
	#
	# accepts:
	# seconds:: the minimal amount of seconds to wait before calling the handler's `call` method.
	# *arg:: any arguments that will be passed to the handler's `call` method.
	# &block:: the block to execute.
	#
	# A block is required.
	#
	# On top of the arguments passed to the `run_after` method, the timer object will be passed as the last agrument to the receiving block.
	#
	# Timed event's time of execution is dependant on the workload and continuous uptime of the process (timed events AREN'T persistent unless you save and reload them yourself).
	#
	# @return [Iodine::TimedEvent] returns the new TimedEvent object.
	def run_every seconds, *args, &block
		timed_job seconds, -1, args, block
	end

	protected

	# Creates a TimedEvent object and adds it to the Timers stack.
	def timed_job seconds, limit = false, args = [], block = nil
		@timer_locker.synchronize {@timers << TimedEvent.new(self, seconds, limit, args, block); @timers.last}
	end
end
