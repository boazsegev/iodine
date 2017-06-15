# This is an example for a Redis pub/sub connector that leverages Iodine's pub/sub
# engine. (Iodine::PubSub::Engine).
#
# This requires the 'hiredis' gem and assumes 'iodine' was already required.

require "hiredis/reader"
require 'uri'

module Iodine
  module PubSub
    class RedisEngine < ::Iodine::PubSub::Engine
      class RedisConnection
        def initialize url, &on_redis_message
          uri = URI(url)
          Iodine.connect(uri.host, uri.port, self)
          @database = uri.path.to_i
          @on_redis_message = on_redis_message
          @reader = ::Hiredis::Reader.new
          @callbacks = []
          @lock = Mutex.new
        end
        def ping
          command "PING"
        end
        def on_open
          command "SELECT", @database if(@database != 0)
        end
        def on_close
          @on_close.call if(@on_close)
        end
        def on_message data
          puts "GOT: #{data}"
          @reader.feed(data)
          reply = nil
          while ((reply = @reader.gets))
            p reply
            on_reply(reply)
          end
        end
        def on_reply reply
          return unless reply
          if reply.is_a?(Array) && reply.count == 3 && reply[0] == 'message'.freeze
            @on_redis_message.call(reply) if @on_redis_message
            return
          end
          cb = nil;
          @lock.synchronize do
            cb = @callbacks.shift
          end
          cb.call(reply) if(cb)
        end

        def command *args, &block
          @lock.synchronize do
            @callbacks << block
          end
          write!("*#{args.length.to_s}\r\n")
          args.each do |arg|
            if(args.is_a? Integer)
              write(":#{arg.to_i}\r\n")
            elsif arg
              arg = arg.to_s
              write!("$#{arg.size.to_s}\r\n#{arg}\r\b")
            else
              write!("$-1\r\n")
            end
          end
        end
      end

      # creates a Iodine::PubSub::Engine Redis pub/sub engine
      def initialize url
        @sub = nil
        @redis = nil
        @url = url
        @perform_publish = method :perform_publish
        # rsub
        # redis
        super()
      end
      # allows access to the underlying Redis connection used for sending commands.
      def redis
        @redis ||= RedisConnection.new(@url)
        @redis = RedisConnection.new @url if(!@redis.open?)
        @redis
      end
      def subscribe(channel, use_pattern)
        return false unless rsub.open?
        rsub.command( (use_pattern ? "PSUBSCRIBE" : "SUBSCRIBE"), channel)
        true
      end
      def unsubscribe(channel, use_pattern)
        return false unless rsub.open?
        rsub.command( (use_pattern ? "PUNSUBSCRIBE" : "UNSUBSCRIBE"), channel)
        true
      end
      def publish(channel, msg, use_pattern)
        puts "publishing... #{msg}"
        return false unless redis.open?
        redis.command( (use_pattern ? "PPUBLISH" : "PUBLISH"), channel, msg)
      end
      protected
      def rsub
        @sub ||= RedisConnection.new(@url, &@perform_publish)
        @sub = RedisConnection.new(@url, &@perform_publish) if(!@sub.open?)
        @sub
      end
      def perform_publish msg
        distribute msg[1], msg[2]
      end
    end
  end
end
