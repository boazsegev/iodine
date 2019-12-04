RSpec.describe 'Transfer-Encoding: chunked', with_app: :echo do
  let(:body_size) { 6 }
  let(:body_string) { SecureRandom.hex(body_size).to_s[0...body_size] }
  let(:io) do
    # ensures theres no size sneaking in
    reader, writer = IO.pipe
    writer.write(body_string)
    writer.close
    reader
  end

  shared_examples 'chunked body' do
    it 'returns the correct body' do
      response = http_post("/", headers: headers, body: io)

      expect(response.headers['Content-Length']).to eql(body_size.to_s)
      expect(response.body.to_s).to eql(body_string)
    end

    it 'returns the correct Content-Length' do
      response = http_post("/", headers: headers, body: io)

      expect(response.headers['Content-Length']).to eql(body_size.to_s)
    end
  end

  context 'when the request is not chunked' do
    let(:headers) { Hash['Content-Length' => body_size] }

    include_examples 'chunked body'
  end

  context 'when the request is chunked' do
    let(:headers) { Hash['Transfer-Encoding' => 'chunked'] }

    include_examples 'chunked body'
  end

  context 'when the header is downcased' do
    let(:headers) { Hash['transfer-encoding' => 'chunked'] }

    include_examples 'chunked body'
  end

  context 'when the body size is long and unevenly chunked', :verbose do
    let(:headers) { Hash['Transfer-Encoding' => 'chunked'] }
    let(:body_size) { 0x4001 }

    include_examples 'chunked body'
  end

  context 'when the body size is long and evenly chunked', :verbose do
    let(:headers) { Hash['Transfer-Encoding' => 'chunked'] }
    let(:body_size) { 0x4000  + 0x4000 }

    include_examples 'chunked body'
  end
end
