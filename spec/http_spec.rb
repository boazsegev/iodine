# frozen_string_literal: true

require 'spec_helper'
require 'support/iodine_test_batch'
require 'net/http'
require 'uri'

# =============================================================================
# Iodine HTTP Integration Tests
#
# One Iodine.listen + one Iodine.start covers all HTTP tests.
# HTTP client calls run from Iodine.async (worker thread) so the IO thread
# is never blocked. All results collected into HTTP_RESULTS, assertions after
# start returns.
#
# IMPORTANT: Iodine.listen survives until process exit — the listening socket
# persists across Iodine.stop/start cycles and is shared by all reactor
# restarts. Call Iodine.listen ONCE at the top level (outside any before block
# or on_state callback), never inside on_state(:start).
# =============================================================================

HTTP_PORT    = (ENV['IODINE_TEST_PORT'] || 19_876).to_i
HTTP_RESULTS = {}

# ---------------------------------------------------------------------------
# NeoRack handler — a single handler covers all test cases via path routing
# ---------------------------------------------------------------------------
module TestHTTPHandler
  def self.on_http(e)
    case e.path
    when '/echo'
      body = e.read.to_s
      e.finish("method=#{e.method} path=#{e.path} query=#{e.query} " \
               "version=#{e.version} peer=#{e.peer_addr} body=#{body}")

    when '/headers'
      e.write_header('X-Echo', e.headers['x-test-input'].to_s)
      e.finish('headers-ok')

    when '/status'
      e.status = 404
      e.finish('not-found-body')

    when '/multi'
      e.finish("multi-#{e.headers['x-seq'].to_s}")

    when '/stream'
      e.write('chunk1 ')
      e.write('chunk2')
      e.finish

    else
      e.status = 404
      e.finish('unknown-path')
    end
  end
end

# Rack-style fallback handler (responds to .call)
module RackHandler
  def self.call(env)
    [200, { 'content-type' => 'text/plain' }, ["rack:#{env['REQUEST_METHOD']}:#{env['PATH_INFO']}"]]
  end
end

HTTP_RACK_PORT = HTTP_PORT + 1

# Register listeners once at file-load time — they persist until process exit,
# surviving any number of Iodine.stop/start cycles.
# If the port is already in use (e.g. from a previous run held in TIME_WAIT),
# override with: IODINE_TEST_PORT=<free_port> bundle exec rspec spec/http_spec.rb
Iodine.listen(url: "http://127.0.0.1:#{HTTP_PORT}",      handler: TestHTTPHandler)
Iodine.listen(url: "http://127.0.0.1:#{HTTP_RACK_PORT}", handler: RackHandler)

# ---------------------------------------------------------------------------
# HTTP test suite — one reactor start, all scenarios run inside one async block
# ---------------------------------------------------------------------------
RSpec.describe 'Iodine HTTP server' do
  # Guard: on_state(:start) callbacks persist across reactor restarts.
  # This flag ensures the HTTP tests only run once.
  HTTP_STARTED = [false]  # rubocop:disable RSpec/LeakyConstantDeclaration

  before(:context) do
    Iodine.verbosity = 0
    Iodine.workers   = 0
    Iodine.threads   = 4   # async HTTP clients need worker threads

    # Capture the test runner as a local proc so it's accessible from the
    # on_state(:start) closure (before(:context) runs in instance context,
    # not class context, so class methods are not directly accessible).
    run_tests = proc do
      base = "http://127.0.0.1:#{HTTP_PORT}"

      # GET /echo?foo=bar
      resp = Net::HTTP.get_response(URI("#{base}/echo?foo=bar"))
      HTTP_RESULTS[:get_status] = resp.code.to_i
      HTTP_RESULTS[:get_body]   = resp.body

      # POST /echo with body
      resp = Net::HTTP.post(URI("#{base}/echo"), 'hello-body', 'Content-Type' => 'text/plain')
      HTTP_RESULTS[:post_status] = resp.code.to_i
      HTTP_RESULTS[:post_body]   = resp.body

      # Custom request header reflected as response header
      req = Net::HTTP::Get.new('/headers')
      req['X-Test-Input'] = 'reflected-value'
      resp = Net::HTTP.start('127.0.0.1', HTTP_PORT) { |h| h.request(req) }
      HTTP_RESULTS[:hdr_echo]   = resp['X-Echo']
      HTTP_RESULTS[:hdr_status] = resp.code.to_i

      # Custom status code
      resp = Net::HTTP.get_response(URI("#{base}/status"))
      HTTP_RESULTS[:custom_status] = resp.code.to_i
      HTTP_RESULTS[:status_body]   = resp.body

      # Multiple sequential requests on one keep-alive connection
      statuses = []
      bodies   = []
      Net::HTTP.start('127.0.0.1', HTTP_PORT) do |http|
        3.times do |i|
          r = http.get("/multi", 'X-Seq' => i.to_s)
          statuses << r.code.to_i
          bodies   << r.body
        end
      end
      HTTP_RESULTS[:multi_statuses] = statuses
      HTTP_RESULTS[:multi_bodies]   = bodies

      # Streaming response (write without finish, then finish)
      resp = Net::HTTP.get_response(URI("#{base}/stream"))
      HTTP_RESULTS[:stream_status] = resp.code.to_i
      HTTP_RESULTS[:stream_body]   = resp.body

      # Rack-style handler on its pre-registered port (listener bound at file load)
      resp = Net::HTTP.get_response(URI("http://127.0.0.1:#{HTTP_RACK_PORT}/rack-path"))
      HTTP_RESULTS[:rack_status] = resp.code.to_i
      HTTP_RESULTS[:rack_body]   = resp.body

    rescue => e
      HTTP_RESULTS[:error] = "#{e.class}: #{e.message}\n#{e.backtrace.first(3).join("\n")}"
    ensure
      Iodine.run_after(100) { Iodine.stop }
    end

    Iodine.on_state(:start) do
      next if HTTP_STARTED[0]
      HTTP_STARTED[0] = true
      # Give the listener a moment to finish binding before hitting it
      Iodine.run_after(50) { Iodine.async { run_tests.call } }
      # Safety watchdog — registered here so it uses the current reactor tick
      Iodine.run_after(5000) { Iodine.stop }
    end

    Iodine.start
  end

  # -------------------------------------------------------------------------
  # Sanity check
  # -------------------------------------------------------------------------
  it 'completes without unexpected errors' do
    expect(HTTP_RESULTS[:error]).to be_nil, "HTTP test error: #{HTTP_RESULTS[:error]}"
  end

  # -------------------------------------------------------------------------
  # GET request
  # -------------------------------------------------------------------------
  describe 'GET request' do
    it 'returns status 200' do
      expect(HTTP_RESULTS[:get_status]).to eq(200)
    end

    it 'e.method returns "GET"' do
      expect(HTTP_RESULTS[:get_body]).to include('method=GET')
    end

    it 'e.path returns the request path' do
      expect(HTTP_RESULTS[:get_body]).to include('path=/echo')
    end

    it 'e.query returns the query string' do
      expect(HTTP_RESULTS[:get_body]).to include('query=foo=bar')
    end

    it 'e.peer_addr returns a non-empty IP address' do
      expect(HTTP_RESULTS[:get_body]).to match(/peer=\d+\.\d+\.\d+\.\d+/)
    end

    it 'e.version returns the HTTP version string' do
      expect(HTTP_RESULTS[:get_body]).to match(/version=HTTP\/\d+\.\d+/)
    end
  end

  # -------------------------------------------------------------------------
  # POST request with body
  # -------------------------------------------------------------------------
  describe 'POST request' do
    it 'returns status 200' do
      expect(HTTP_RESULTS[:post_status]).to eq(200)
    end

    it 'e.method returns "POST"' do
      expect(HTTP_RESULTS[:post_body]).to include('method=POST')
    end

    it 'e.read returns the request body' do
      expect(HTTP_RESULTS[:post_body]).to include('body=hello-body')
    end
  end

  # -------------------------------------------------------------------------
  # Headers
  # -------------------------------------------------------------------------
  describe 'request and response headers' do
    it 'handler reads a custom request header via e.headers[]' do
      expect(HTTP_RESULTS[:hdr_echo]).to eq('reflected-value')
    end

    it 'returns 200 for the headers endpoint' do
      expect(HTTP_RESULTS[:hdr_status]).to eq(200)
    end
  end

  # -------------------------------------------------------------------------
  # Custom status via e.status=
  # -------------------------------------------------------------------------
  describe 'custom response status' do
    it 'e.status= changes the response code' do
      expect(HTTP_RESULTS[:custom_status]).to eq(404)
    end

    it 'e.finish(body) sends the body with the custom status' do
      expect(HTTP_RESULTS[:status_body]).to eq('not-found-body')
    end
  end

  # -------------------------------------------------------------------------
  # Multiple sequential requests (keep-alive)
  # -------------------------------------------------------------------------
  describe 'multiple sequential requests on one connection' do
    it 'all three return 200' do
      expect(HTTP_RESULTS[:multi_statuses]).to eq([200, 200, 200])
    end

    it 'each request receives the correct body' do
      expect(HTTP_RESULTS[:multi_bodies]).to eq(['multi-0', 'multi-1', 'multi-2'])
    end
  end

  # -------------------------------------------------------------------------
  # Streaming response
  # -------------------------------------------------------------------------
  describe 'streaming response (e.write + e.finish)' do
    it 'returns status 200' do
      expect(HTTP_RESULTS[:stream_status]).to eq(200)
    end

    it 'body contains all written chunks' do
      expect(HTTP_RESULTS[:stream_body]).to include('chunk1')
      expect(HTTP_RESULTS[:stream_body]).to include('chunk2')
    end
  end

  # -------------------------------------------------------------------------
  # Rack-style handler
  # -------------------------------------------------------------------------
  describe 'Rack-style handler (.call interface)' do
    it 'returns status 200' do
      expect(HTTP_RESULTS[:rack_status]).to eq(200)
    end

    it 'body is generated by the Rack handler' do
      expect(HTTP_RESULTS[:rack_body]).to eq('rack:GET:/rack-path')
    end
  end
end
