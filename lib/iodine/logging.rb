module Iodine
	public

	# Gets the logging object and allows you to call logging methods (i.e. `Iodine.log.info "Running"`).
	def logger log = nil
		@logger
	end

	# logs info
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def info data, &block
		@logger.info data, &block if @logger
		data
	end
	# logs debug info
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def debug data, &block
		@logger.debug data, &block if @logger
		data
	end
	# logs warning
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def warn data, &block
		@logger.warn data, &block if @logger
		data
	end
	# logs errors
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def error data, &block
		@logger.error data, &block if @logger
		data
	end
	# logs a fatal error
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def fatal data, &block
		@logger.fatal data, &block if @logger
		data
	end


	protected

	@logger = Logger.new(STDOUT)

end
