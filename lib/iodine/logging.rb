require "logger"

class Iodine
	public

	# sets a new logger. Should be a member of the Ruby `Logger` class.
	def self.logger= new_logger
		@logger = new_logger
	end

	# returns the logger object.
	def self.logger
		@logger
	end

	# the initial logger object logs to STDOUT.
	@logger = ::Logger.new(STDOUT)

	# logs info
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def self.info data, &block
		@logger.info data, &block if @logger
		data
	end
	# logs debug info
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def self.debug data, &block
		@logger.debug data, &block if @logger
		data
	end
	# logs warning
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def self.warn data, &block
		@logger.warn data, &block if @logger
		data
	end
	# logs errors
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def self.error data, &block
		@logger.error data, &block if @logger
		data
	end
	# logs a fatal error
	# @return [String, Exception, Object] always returns the Object sent to the log.
	def self.fatal data, &block
		@logger.fatal data, &block if @logger
		data
	end
	# logs a raw text
	# @return [String] always returns the Object sent to the log.
	def self.log raw_text
		@logger << raw_text if @logger
		raw_text
	end

end
