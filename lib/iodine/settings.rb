module Iodine
	public

	#######################
	## Settings - methods that change the way Iodine behaves should go here.

	# Sets the logging object, which needs to act like `Logger`. The default logger is `Logger.new(STDOUT)`.
	def logger= obj
		@logger = obj
	end

	# Sets the number of threads in the thread pool used for executing the tasks. Defaults to 1 thread.
	def threads= count
		@thread_count = count
	end

	# Gets the number of threads in the thread pool used for executing the tasks. Returns `nil` unless previously set or Iodine is running.
	def threads
		@thread_count
	end

	# Sets the number of processes that should be spawned in Server mode. Defaults to 1 (no processes spawned).
	#
	# * Forking (spwaning processes) might NOT work on all systems (forking is supported by Ruby on Unix systems).
	# * Please make sure your code is safe to fork into different processes. For instance, Websocket broadcasting and unicasting won't work across different processes unless synced using an external Pub/Sub service/database such as Redis.
	# * Forking might cause some tasks (such as time based tasks) to be performed twice (once for each process). This is a feature. To avoid duplicated task performance, use a task (delayed execution) to initialize any tasks you want to perform only once. While the initial time based tasks and the server are shared across processes, the initial task stack will only run on the main process.
	def processes= count
		@spawn_count = count
	end

	# Gets the number of processes that should be spawned in Server mode. Defaults to 1 (no processes spawned). See {Iodine#processes=}.
	def processes
		@spawn_count
	end

	# Sets Iodine's server port. Defaults to the command line `-p` argument, or the ENV['PORT'] or 3000 (in this order).
	def port= port
		@port = port
	end
	# Gets Iodine's server port. Defaults to the command line `-p` argument, or the ENV['PORT'] or 3000 (in this order).
	def port
		@port
	end
	# Sets the IP binding address. Defaults to the command line `-ip` argument, or the ENV['IP'] or 0.0.0.0 (in this order).
	def bind= address
		@bind = address
	end
	# Gets the IP binding address. Defaults to the command line `-ip` argument, or the ENV['IP'] or 0.0.0.0 (in this order).
	def bind
		@bind
	end

	# Sets the Protocol the Iodine Server will use. Should be a child of {Iodine::Protocol}. Defaults to nil (no server).
	#
	# If the protocol passed does NOT inherit from {Iodine::Protocol}, Iodine will cycle without initiating a server until stopped (TimedEvent mode).
	def protocol= protocol
		@stop = protocol ? false : true
		@protocol = protocol
	end
	# Returns the cutrently active Iodine protocol (if exists).
	def protocol
		@protocol
	end

	# Sets the SSL flag, so that Iodine will require that new connection be encrypted.
	# Defaults to false unless the `ssl` command line flag is present.
	def ssl= required
		@ssl = required && true
	end
	# Returns true if Iodine will require that new connection be encrypted.
	def ssl
		@ssl
	end

	# Sets the SSL Context to be used when using an encrypted connection. Defaults to a self signed certificate and no verification.
	#
	# Manually setting the context will automatically set the SSL flag,
	# so that Iodine will require encryption for new incoming connections.
	def ssl_context= context
		@ssl = true
		@ssl_context = context
	end

	# Gets the SSL Context to be used when using an encrypted connection.
	def ssl_context
		@ssl_context ||= init_ssl_context
	end

	# Sets the an SSL Protocol Hash (`'name' => Protocol`), allowing dynamic Protocol Negotiation.
	# At the moment only NPN is supported. ALPN support should be established in a future release.
	#
	# * please notice that using allowing dynamic Protocol Negotiation could cause unexpected protocol choices when attempting to implement Opportunistic Encryption with {Iodine::SSLConnector}.
	def ssl_protocols= protocol_hash
		raise TypeError, "Iodine.ssl_protocols should be a Hash with Strings for keys (protocol identifiers) and Classes as values (Protocol classes)." unless protocol_hash.is_a?(Hash)
		@ssl = true
		ssl_context.npn_protocols = protocol_hash.keys
		@ssl_protocols = protocol_hash
	end

	# Gets the SSL Protocol Hash used for 
	def ssl_protocols
		@ssl_protocols
	end


	protected

	#initializes a default SSLContext
	def init_ssl_context
		ssl_context = OpenSSL::SSL::SSLContext.new
		ssl_context.set_params verify_mode: OpenSSL::SSL::VERIFY_NONE
		ssl_context.cert_store = OpenSSL::X509::Store.new
		ssl_context.cert_store.set_default_paths
		ssl_context.session_cache_mode = OpenSSL::SSL::SSLContext::SESSION_CACHE_NO_INTERNAL #SESSION_CACHE_OFF
		ssl_context.cert, ssl_context.key = create_cert
		ssl_context
	end

	#creates a self-signed certificate
	def create_cert bits=2048, cn=nil, comment='a self signed certificate for when we only need encryption and no more.'
		unless cn
			host_name = Socket::gethostbyname(Socket::gethostname)[0].split('.')
			cn = String.new
			host_name.each {|n| cn << "/DC=#{n}"}
			cn << "/CN=Iodine.#{host_name.join('.')}"
		end			
		# cn ||= "CN=#{Socket::gethostbyname(Socket::gethostname)[0] rescue Socket::gethostname}"

		time = Time.now
		rsa = OpenSSL::PKey::RSA.new(bits)
		cert = OpenSSL::X509::Certificate.new
		cert.version = 2
		cert.serial = 1
		name = OpenSSL::X509::Name.parse(cn)
		cert.subject = name
		cert.issuer = name
		cert.not_before = time
		cert.not_after = time + (365*24*60*60)
		cert.public_key = rsa.public_key

		ef = OpenSSL::X509::ExtensionFactory.new(nil,cert)
		ef.issuer_certificate = cert
		cert.extensions = [
		ef.create_extension("basicConstraints","CA:FALSE"),
		ef.create_extension("keyUsage", "keyEncipherment"),
		ef.create_extension("subjectKeyIdentifier", "hash"),
		ef.create_extension("extendedKeyUsage", "serverAuth"),
		ef.create_extension("nsComment", comment),
		]
		aki = ef.create_extension("authorityKeyIdentifier",
		                        "keyid:always,issuer:always")
		cert.add_extension(aki)
		cert.sign(rsa, OpenSSL::Digest::SHA1.new)

		return cert, rsa
	end

end
