# frozen_string_literal: true

require 'stringio' # Used internally as a default RackIO
require 'socket'  # TCPSocket is used internally for Hijack support
require 'logger'  # rack.logger became a required field for Rack

require_relative 'iodine/version'

require 'iodine/iodine'

# Support common ENV options for setting concurrency.
Iodine.threads = ENV['RAILS_MAX_THREADS'].to_i if ENV['RAILS_MAX_THREADS'] && Iodine.threads.zero?
Iodine.workers = ENV['WEB_CONCURRENCY'].to_i if ENV['WEB_CONCURRENCY'] && Iodine.workers.zero?
Iodine.threads = ENV['WEB_THREADS'].to_i if ENV['WEB_THREADS'] && Iodine.threads.zero?
Iodine.workers = ENV['WEB_WORKERS'].to_i if ENV['WEB_WORKERS'] && Iodine.workers.zero?
Iodine.threads = ENV['THREADS'].to_i if ENV['THREADS'] && Iodine.threads.zero?
Iodine.workers = ENV['WORKERS'].to_i if ENV['WORKERS'] && Iodine.workers.zero?

module Iodine
  class Base
    # Trap SIGINT only if it wasn't trapped beforehand.
    def self.trap_sigint
      old_trap = trap('SIGINT') { puts "(#{Process.pid}) Received Ctrl-C and shutting down iodine." }
      trap('SIGINT', &old_trap) if old_trap && old_trap.respond_to?(:call)
    end
  end

  module PubSub
    class Engine
      # Base publish method — subclasses override this to route messages to
      # external brokers. The default implementation is a no-op (the built-in
      # CLUSTER and LOCAL engines handle delivery in C).
      #
      # @param msg [Iodine::PubSub::Message] the message to publish
      # @return [void]
      def publish(msg); end
    end
  end
end

# Trap SIGINT only if it wasn't trapped beforehand
Iodine::Base.trap_sigint

# NeoRack Support
unless defined?(Server)
  # NeoRack support requires Iodine to set the name Server to the Iodine NeoRack server module.
  Server = Iodine
  module Server
    # NeoRack support requires Server::Event to map to the class implementing the NeoRack events.
    Event = Iodine::Connection
  end
end

### Automatic Sequel support for forking (preventing connection sharing)
Iodine.on_state(:before_fork) do
  if defined?(Sequel)
    begin
      Sequel::DATABASES.each(&:disconnect)
    rescue StandardError
    end
  end
  if defined?(ActiveRecord)
    begin
      ActiveRecord::Base.connection_handlers.each do |handler|
        handler.clear_all_connections!
      end
    rescue StandardError
    end
  end
end

### Initialize Redis if set in CLI
if Iodine::Base::CLI['-r']
  Iodine::PubSub.default = Iodine::PubSub::Engine::Redis.new(
    Iodine::Base::CLI['-r'],
    ping: (Iodine::Base::CLI['-rp'] ? Iodine::Base::CLI['-rp'].to_i : nil)
  )
end
