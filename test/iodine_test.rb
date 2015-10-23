require 'test_helper'

Iodine.warn "Iodine only tests the HPACK encode/decode automatically. Most other tests are performed manualy at this time."

class IodineTest < Minitest::Test
  def test_that_it_has_a_version_number
    refute_nil ::Iodine::VERSION
  end

  def test_that_settings_are_settable
  	Iodine.ssl = false
  	assert_equal Iodine.ssl , false, "Iodine.ssl - SSL/TLS setting failed"
  	Iodine.ssl = true
  	assert_equal Iodine.ssl , true, "Iodine.ssl - SSL/TLS setting failed"
  	Iodine.port = 3000
  	assert_equal Iodine.port , 3000, "Iodine.port - Port setting failed"
  	Iodine.port = 3030
  	assert_equal Iodine.port , 3030, "Iodine.port - Port setting failed"
  	Iodine.bind = "localhost"
  	assert_equal Iodine.bind , "localhost", "Iodine.bind - IP setting failed"
  	Iodine.bind = "0.0.0.0"
  	assert_equal Iodine.bind , "0.0.0.0", "Iodine.bind - IP setting failed"
  	assert Iodine.instance_exec { @ssl_context.nil? } , "SSL context should start as `nil`."
  	initialized_context = Iodine.ssl_context
  	refute_nil Iodine.instance_exec { @ssl_context } , "SSL context should automatically initiate lazily."
  	assert_equal Iodine.ssl_context, Iodine.instance_exec { @ssl_context } , "SSL context should be returned."
  	Iodine.ssl_context = nil
  	assert Iodine.instance_exec { @ssl_context.nil? } , "SSL context should be settable."
  	# write tests for threads, processes, protocol, ssl_protocols
  	# and, maybe in a different file, http2, session_token, oh_http, on_websockets, 
  end

end
