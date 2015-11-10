module Iodine
	module Http
		# session management for Iodine's Http Server
		module SessionManager

			module MemSessionStorage
				class SessionObject < Hash
					def initialize id
						super()
						self[:__session_id] = id		
					end
					# returns the session id (the session cookie value).
					def id
						self[:__session_id]
					end
				end
				@mem_storage = {}
				def self.fetch key
					@mem_storage[key] ||= SessionObject.new(key)
				end
			end
			module FileSessionStorage
				class SessionObject
					# called by the Plezi framework to initiate a session with the id requested
					def initialize id
						@filename = File.join Dir.tmpdir, "iodine_#{Iodine::Http.session_token}_#{id}"
						@data ||= {}
						@id = id
					end
					# returns the session id (the session cookie value).
					def id
						@id
					end
					# Get a key from the session data store.
					#
					# Due to different considirations, all keys will be converted to strings, so that `"name" == :name` and `1234 == "1234"`.
					# If you store two keys that evaluate as the same string, they WILL override each other.
					def [] key
						key = key.to_s
						load
						@data[key]
					end
					alias :fetch :[]

					# Stores a key in the session's data store.
					#
					# Due to different considirations, all keys will be converted to strings, so that `"name" == :name` and `1234 == "1234"`.
					# If you store two keys that evaluate as the same string, they WILL override each other.
					def []= key, value
						return delete key if value.nil?
						key = key.to_s
						load
						@data[key] = value
						save
						value
					end
					alias :store :[]=

					# @return [Hash] returns a shallow copy of the current session data as a Hash.
					def to_h
						load
						@data.dup
					end

					# @return [String] returns the Session data in YAML format.
					def to_s
						load
						@data.to_yaml
					end

					# Removes a key from the session's data store.
					def delete key
						load
						ret = @data.delete key.to_s
						save
						ret
					end

					# Clears the session's data.
					def clear
						@data.clear
						nil
					end

					# Forced the session's data to be reloaded
					def refresh
						load
					end
					protected
					# def destroy
					# 	# save data to tmp-file
					# 	File.delete @filename if ::File.file?(@filename) # && !::File.directory?(@filename)
					# end
					def save
						# save data to tmp-file
						IO.write @filename, @data.to_yaml
					end
					def load
						@data = YAML.load IO.read(@filename) if ::File.file?(@filename)
					end
				end
				def self.fetch key
					SessionObject.new key
				end
			end

			module_function
			# returns a session object
			def get id
				storage.fetch(id)
			end
			# Sets the session storage system, to allow for different storage systems.
			#
			# A Session Storage system must answer only one methods:
			# fetch(id):: returns a Hash like session object with all the session's data or a fresh session object if the session object did not exist before
			#
			# The Session Object should update the storage whenever data is saved to the session Object.
			# This is important also because a websocket 'session' could exist simultaneously with other HTTP requests (multiple browser windows) and the data should be kept updated at all times.
			def storage= session_storage = nil
				case session_storage
				when :file, nil
					@storage = Iodine::Http::SessionManager::FileSessionStorage
				when :mem
					@storage = Iodine::Http::SessionManager::MemSessionStorage
				else
					@storage = session_storage
				end
			end

			# returns the current session storage system.
			def storage
				@storage ||= Iodine::Http::SessionManager::FileSessionStorage
			end
		end
	end
end
# A hash like interface for storing request session data.
# The store must implement:
# store(key, value) (aliased as []=);
# fetch(key, default = nil) (aliased as []);
# delete(key);
# clear;
# id(); (returns the session id)