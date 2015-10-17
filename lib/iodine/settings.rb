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

	# Sets the server port. Defaults to the runtime `-p` argument, or the ENV['PORT'] or 3000 (in this order).
	def port= port
		@port = port
	end
	# Sets the IP binding address. Defaults to the runtime `-ip` argument, or the ENV['IP'] or 0.0.0.0 (in this order).
	def bind= address
		@bind = address
	end

	# Sets the Protocol the Iodine Server will use. Should be a child of Protocol or SSLProtocol. Defaults to nil (no server).
	def protocol= protocol
		@stop = protocol ? false : true
		@protocol = protocol
	end
	# Returns the cutrently active Iodine protocol (if exists).
	def protocol
		@protocol
	end

	# Sets the SSL Context to be used when using an SSLProtocol. Defaults to a self signed certificate.
	def ssl_context= context
		@ssl_context = context
	end
	# Gets the SSL Context to be used when using an SSLProtocol.
	def ssl_context
		@ssl_context ||= init_ssl_context
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
			cn = ''
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
