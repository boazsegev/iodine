RSpec.describe 'Transfer-Encoding Header' do
  include Spec::Support::IodineServer

  around(:example) { |ex| with_app(:body_size) { ex.run } }

  let(:body_size) { 6 }
  let(:io) { StringIO.new("a" * body_size) }

  shared_examples 'body size' do
    it 'returns the correct body size when chunked encoding is used' do
      response = http_post("/", headers: headers, body: io)

      expect(response.body.to_s).to eql("body_size=#{body_size}")
    end
  end

  context 'when the request is not chunked' do
    let(:headers) { Hash[] }

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

  context 'when the body size is long' do
    let(:headers) { Hash['Transfer-Encoding' => 'chunked'] }
    let(:body_size) { 60000 }

    include_examples 'body size'
  end
end
