module Iodine
  # Iodine's {Iodine::TLS} instances hold SSL/TLS settings for secure connections.
  #
  # This allows both secure client connections and secure server connections to be established.
  #
  #     tls = Iodine::TLS.new "localhost" # self-signed certificate
  #     Iodine.listen service: "http", handler: APP, tls: tls
  #
  # Iodine abstracts away the underlying SSL/TLS library to minimize the risk of misuse and insecure settings.
  #
  # Calling TLS methods when no SSL/TLS library is available should result in iodine crashing. This is expected behavior.
  #
  # At the moment, only OpenSSL is supported. BearSSL support is planned.
	class TLS
	end
end
