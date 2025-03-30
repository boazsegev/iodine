# This is an example configuration file
#
# When hot code swapping is enabled, only the configuration file will run code on the root (master) process.

module MyConfig
	def self.timer
		# a false return value will cancel the timer.
		return false unless Iodine.master?
		puts("Heat Beat")
	end
end

Iodine.run_after(2000, -1, MyConfig.method(:timer))

# Pub/Sub engines are a classical use-case for a configuration file.
#
class LogPubSub < Iodine::PubSub::Engine
	def subscribe ch
		puts "(#{Process.pid}) #{ch} has listeners (created)"
	end
	def psubscribe ch
		puts "(#{Process.pid}) (pattern) #{ch} has listeners (created)"
	end
	def unsubscribe ch
		puts "(#{Process.pid}) #{ch} has no more listeners (destroyed)"
	end
	def punsubscribe ch
		puts "(#{Process.pid}) (pattern) #{ch} has no more listeners (destroyed)"
	end
	def publish m
		puts "(#{Process.pid}) Someone published to #{m.channel}"
		Iodine.publish channel: m.channel, message: m.message, filter: m.filter, engine: Iodine::PubSub::CLUSTER
	end
end
Iodine::PubSub.default = LogPubSub.new
