RSpec.describe 'Transfer-Encoding Header' do
  include Spec::Support::IodineServer

  around(:example) { |ex| with_app(:body_size) { ex.run } }

  let(:body_size) { 6 }
  let(:body_string) { SecureRandom.hex(body_size).to_s[0...body_size] }
  let(:io) do
    # ensures theres no size sneaking in
    reader, writer = IO.pipe
    writer.write(body_string)
    writer.close
    reader
  end

  shared_examples 'body size' do
    it 'returns the correct body size when chunked encoding is used' do
      response = http_post("/", headers: headers, body: io)

      expect(response.body.to_s).to eql("body_size=#{body_size}")
    end
  end

  context 'when the request is not chunked' do
    let(:headers) { Hash['Content-Length' => body_size] }

    include_examples 'body size'
  end

  context 'when the request is chunked' do
    let(:headers) { Hash['Transfer-Encoding' => 'chunked'] }

    include_examples 'body size'
  end

  context 'when the header is downcased' do
    let(:headers) { Hash['transfer-encoding' => 'chunked'] }

    include_examples 'body size'
  end

  context 'when the body size is long and unevenly chunked' do
    let(:headers) { Hash['Transfer-Encoding' => 'chunked'] }
    let(:body_size) { 0x4001 }

    include_examples 'body size'
  end

  context 'when the body size is long and evenly chunked' do
    let(:headers) { Hash['Transfer-Encoding' => 'chunked'] }
    let(:body_size) { 0x4000  + 0x4000 }

    include_examples 'body size'
  end
end
