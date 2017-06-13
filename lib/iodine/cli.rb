module Iodine
  module Base
    # Command line interface
    module CLI

      module_functions

      def print_help
        puts <<-EOS

Iodine's HTTP/Websocket server version #{Iodine::VERSION}

Use:

    iodine <options> <filename>

Both <options> and <filename> are optional.

Available options:
 -p          Port number. Default: 3000.
 -t          Number of threads. Default: CPU core count.
 -w          Number of worker processes. Default: CPU core count.
 -www        Public folder for static file serving. Default: nil (none).
 -v          Log responses. Default: never log responses.
 -warmup     Warmup invokes autoloading (lazy loading) during server startup.
 -tout       HTTP inactivity connection timeout. Default: 5 seconds.
 -maxbd      Maximum Mb per HTTP message (max body size). Default: 50Mib.
 -maxms      Maximum Bytes per Websocket message. Default: 250Kib.
 -ping       Websocket ping interval in seconds. Default: 40 seconds.
 <filename>  Defaults to: config.ru

Example:

    iodine -p 80

    iodine -p 8080 path/to/app/conf.ru

    iodine -p 8080 -w 4 -t 16

EOS
      end


      def try_file filename
        begin
          return Rack::Builder.parse_file filename
        rescue
          return [nil, nil]
        end
      end



      def call

        #!/usr/bin/env ruby

        require 'iodine'
        require 'rack'

        if ARGV[0] =~ /(\-\?)|(help)|(\?)|(h)|(\-h)$/
          return print_help
        end

        app, opt = nil, nil
        filename = (ARGV[-2].to_s[0] != '-' && ARGV[-1].to_s[0] != '-' && ARGV[-1])
        if filename
          app, opt = try_file filename;
          puts "Couldn't find #{filename}, testing for config.ru" if !opt
        end
        app, opt = try_file "config.ru";
        unless app || opt
          puts "WARNING: Ruby application not found#{ filename ? " - missing both #{filename} and config.ru" : " - missing config.ru"}."
          if ARGV.index('-www') && ARGV[ARGV.index('-www') + 1]
            puts "         Running only static file service."
            opt = Rack::Server::Options.new.parse!([])
          else
            puts "For help run:"
            puts "       iodine -?"
            return
          end
        end

        if ARGV.index('-maxbd') && ARGV[ARGV.index('-maxbd') + 1]
          Iodine::Rack.max_body_size = ARGV[ARGV.index('-maxbd') + 1].to_i
        end
        if ARGV.index('-maxms') && ARGV[ARGV.index('-maxms') + 1]
          Iodine::Rack.max_msg_size = ARGV[ARGV.index('-maxms') + 1].to_i
        end
        if ARGV.index('-ping') && ARGV[ARGV.index('-ping') + 1]
          Iodine::Rack.ws_timeout = ARGV[ARGV.index('-ping') + 1].to_i
        end
        if ARGV.index('-www') && ARGV[ARGV.index('-www') + 1]
          Iodine::Rack.public = ARGV[ARGV.index('-www') + 1]
        end
        if ARGV.index('-tout') && ARGV[ARGV.index('-tout') + 1]
          Iodine::Rack.timeout = ARGV[ARGV.index('-tout') + 1].to_i
          puts "WARNNING: Iodine::Rack.timeout set to 0 (ignored, timeout will be ~5 seconds)."
        end
        Iodine::Rack.log = true if ARGV.index('-v')
        Iodine::Rack.log = false if ARGV.index('-q')
        Iodine.warmup if ARGV.index('-warmup')
        Iodine::Rack.run(app, opt)
      end
    end
  end
end
