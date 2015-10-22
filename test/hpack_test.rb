require 'test_helper'

class HPackTest < Minitest::Test

	def hpack &block
		Iodine::Http::Http2::HPACK.new.instance_exec &block
	end
	def test_that_it_decodes_numbers
		# example from specs
		data = StringIO.new("\x1F\x9A\n".force_encoding('binary'))
		assert( 1337 == (hpack { extract_number(data, data.getbyte, 3) } ))
		# example from specs
		data = StringIO.new("\xaa".force_encoding('binary'))
		assert( 10 == (hpack { extract_number(data, data.getbyte, 3) } ))
	end
	def test_that_it_encodes_numbers
		# reverse the examples above...
		assert( (hpack { pack_number(1337, 0, 3) } ) == "\x1F\x9A\n".force_encoding(::Encoding::ASCII_8BIT))
		assert( (hpack { pack_number(10, 5, 3) } ) == "\xaa".force_encoding(::Encoding::ASCII_8BIT))
	end
	def test_that_it_unpacks_strings
		data = StringIO.new "\ncustom-key\rcustom-header".force_encoding(::Encoding::ASCII_8BIT)
		assert((hpack { extract_string data } ) == "custom-key".force_encoding(::Encoding::ASCII_8BIT))
		assert( (hpack { extract_string data } ) == "custom-header".force_encoding(::Encoding::ASCII_8BIT))
	end
	def test_that_it_packs_strings
		assert((hpack { pack_string("test", false) } ) == "\x04test".force_encoding(::Encoding::ASCII_8BIT))
		assert((hpack { pack_string("custom-key", false) +  pack_string("custom-header", false)} ) == "\ncustom-key\rcustom-header".force_encoding(::Encoding::ASCII_8BIT))
	end
	def test_that_it_decodes_strings
		# write hoffman
	end
	def test_that_it_encodes_strings
		# write hoffman
	end
	def test_that_hoffman_encoding_is_symmetric
		# write hoffman
		data = "abcdefghijklmnopqrstuvwxyz01234567890-=\`\\][\t\b"
		assert_equal data*2, (hpack { deflated_data = StringIO.new(pack_string(data,true) + pack_string(data,true)); extract_string(deflated_data) +  extract_string(deflated_data)})
	end
	def test_that_it_unpacks_headers
		data ="@\ncustom-key\rcustom-header".force_encoding(::Encoding::ASCII_8BIT)
		headers = hpack { decode(data) }
		assert(headers['custom-key'] == 'custom-header')
	end
	def test_that_it_packs_headers
		original_headers = {}
		original_headers['custom-key'] = 'custom-header'
		original_headers[:path] = '/'
		original_headers[:authority] = 'www.example.com'
		original_headers['set-cookie'] = 'name=value;'
		stream = hpack { encode(original_headers) }
		headers = hpack { decode(stream) }
		assert_equal headers, original_headers
	end
	def test_that_it_decodes_headers
		data = "\x82\x86\x84A\x8C\xF1\xE3\xC2\xE5\xF2:k\xA0\xAB\x90\xF4\xFF".force_encoding(::Encoding::ASCII_8BIT)
		headers = hpack { decode(data) }
		assert(headers == {method: 'GET', scheme: 'http', path: '/', authority: 'www.example.com'})
	end

  # def test_it_does_something_useful
  #   assert false
  # end
end


describe Iodine::Http::Http2::HPACK do
	before do
		@context = Iodine::Http::Http2::HPACK.new
	end

	describe "HPACK - encoding/dencoding a number" do
		it "Will correctly encode/decode a number." do
			(@context.instance_eval { extract_number(StringIO.new(''), 254, 1) }).must_equal 126
			(@context.instance_eval { extract_number(StringIO.new(0.chr), 255, 1) }).must_equal 127
			(@context.instance_eval { extract_number(StringIO.new(128.chr + 1.chr), 255, 1) }).must_equal 255
			(@context.instance_eval { pack_number 16800722, 1, 1 }).must_equal "\xFF\xD3\xB6\x81\b".force_encoding('binary')
			(@context.instance_eval { extract_number StringIO.new("\xD3\xB6\x81\b".force_encoding('binary')), 0x7F, 1 }).must_equal 16800722
			(@context.instance_eval { pack_number 7, 1, 1 }).must_equal "\x87".force_encoding('binary')
			(@context.instance_eval { extract_number StringIO.new("\x87".force_encoding('binary')), 0x87, 1 }).must_equal 7
		end
	end
	describe "HPACK - (Un)Packing a header field" do
		it "Will correctly find an indexed field." do
			(@context.instance_eval { decode_field StringIO.new("\x82") }).must_equal [':method', 'GET']
		end
		it "Will correctly decode a literal field." do
			(@context.instance_eval { decode_field StringIO.new("@\ncustom-key\rcustom-header") }).must_equal ['custom-key', 'custom-header']
		end
		it "Will correctly decode an indexed literal field." do
			(@context.instance_eval { decode_field StringIO.new("\x04\f/sample/path") }).must_equal [':path', '/sample/path']
		end

		it "Will correctly decode a Huffman encoded header list." do
			list = StringIO.new "\x82\x86\x84A\x8C\xF1\xE3\xC2\xE5\xF2:k\xA0\xAB\x90\xF4\xFFmore_data"
			(@context.instance_eval { decode_field list }).must_equal [':method', 'GET']
			(@context.instance_eval { decode_field list }).must_equal [':scheme', 'http']
			(@context.instance_eval { decode_field list }).must_equal [':path', '/']
			(@context.instance_eval { decode_field list }).must_equal [':authority', 'www.example.com']
			# chack for reader overflow
			list.read.must_equal 'more_data'
			# check for dynamic table update
			(@context.instance_eval { @decoding_list[62] }).must_equal [':authority', 'www.example.com']
		end

		it "Will correctly decode a header buffer." do
			headers = @context.decode "\x82\x82\x86\x84A\x8C\xF1\xE3\xC2\xE5\xF2:k\xA0\xAB\x90\xF4\xFF"
			headers[:path].must_equal '/'
			headers[:method].must_equal ['GET', 'GET']
			headers[:authority].must_equal 'www.example.com'
			headers[:scheme].must_equal 'http'
		end
	end
	# describe "HPACK - encoding a number" do
	# 	it "Will correctly encode a number." do
	# 		(@context.instance_eval { pack_number(number, prefix) }).must_equal "literal string encoding"
	# 	end
	# end
end
