require 'http'

module Spec
  module Support
    module IodineServer
      def http_client
        HTTP.timeout(3)
      end

      def http_get(path, *args)
        http_client.get("http://localhost:#{server_port}#{path}", *args)
      end

      def http_post(path, *args)
        http_client.get("http://localhost:#{server_port}#{path}", *args)
      end

      def spawn_with_test_log(cmd)
        test_log = ENV.key?('VERBOSE') ? STDERR : File.open('spec/log/test.log', 'a+')

        Bundler.with_clean_env do
          Process.spawn(cmd, out: test_log, err: test_log)
        end
      end

      def server_port
        2222
      end

      def wait_until_iodine_ready
        tries = 0

        loop do
          begin
            sleep 0.1
            Socket.tcp('localhost', server_port, connect_timeout: 1) {}
            break
          rescue Errno::ECONNREFUSED
            raise "Could not start iodine, please check spec/log/test.log for more info" if tries > 10
            tries += 1
          end
        end
      end

      def start_iodine_with_app(name)
        filename = "spec/support/apps/#{name}.ru"
        raise "test rack file (#{name}) does not exist" unless File.exist?(filename)
        pid = spawn_with_test_log("bundle exec exe/iodine -p #{server_port} -w 1 -t 1 #{filename}")
        wait_until_iodine_ready
        pid
      end

      def with_app(name)
        pid = start_iodine_with_app(name)

        begin
          yield if block_given?
        ensure
          if !pid.nil?
            Process.kill 'SIGINT', pid
            Process.wait pid
          end
        end
      end
    end
  end
end
