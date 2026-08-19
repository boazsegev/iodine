# frozen_string_literal: true

require 'socket'
require 'timeout'
require 'uri'

# Blocking TCP helpers for integration tests that depend on a local service.
module BlockingTcpSocket
  DEFAULT_TIMEOUT = 1

  module_function

  # Returns true only when +url+ accepts a RESP3 HELLO handshake.
  # This preflight uses its own short-lived socket; it never sends a second
  # HELLO through the engine being tested.
  def resp3_available?(url, timeout: DEFAULT_TIMEOUT)
    uri = URI.parse(url)
    Socket.tcp(uri.host || 'localhost', uri.port || 6379, connect_timeout: timeout) do |socket|
      socket.write(resp_command(*hello_arguments(uri)))
      Timeout.timeout(timeout) { socket.readpartial(1) } == '%'
    end
  # NOTE: IO::TimeoutError (raised by Socket.tcp on connect_timeout expiry)
  # is NOT a subclass of Timeout::Error and must be rescued separately.
  # Windows CI times out on missing localhost services instead of refusing.
  rescue URI::InvalidURIError, SocketError, SystemCallError, Timeout::Error, EOFError, IO::TimeoutError
    false
  end

  def hello_arguments(uri)
    arguments = ['HELLO', '3']
    arguments.concat(['AUTH', uri.user || 'default', uri.password]) if uri.password
    arguments
  end
  private_class_method :hello_arguments

  def resp_command(*arguments)
    "*#{arguments.length}\r\n" + arguments.map { |argument|
      "$#{argument.bytesize}\r\n#{argument}\r\n"
    }.join
  end
  private_class_method :resp_command
end
