# frozen_string_literal: true

require 'spec_helper'
require 'net/http'
require 'uri'
require 'json'
require 'timeout'

# =============================================================================
# NeoRack `params` extension tests (Iodine::Connection#params / #query2params
# / #body2params)
#
# Own listener on PARAMS_PORT, registered once at file-load time (see the
# "Iodine.listen survives until process exit" note in http_spec.rb).
# All client calls run in a single async block; assertions run afterwards.
# =============================================================================

PARAMS_PORT = (ENV["IODINE_TEST_PORT"] || 19_876).to_i + 20  # +10 is taken by connection_spec
PARAMS_RESULTS = {}
PARAMS_ESCAPED_UPLOADS = Hash.new { |h, k| h[k] = [] }
PARAMS_HASH_SUBCLASS = Class.new(Hash)

# Handler: routes return a JSON rendering of the params Hash (or a report).
module ParamsHandler
  module_function

  def json(e, obj)
    e.finish(Iodine::JSON.stringify(obj))
  end

  def upload_report(up)
    frozen_error = begin
      up.read(1, 'frozen')
      false
    rescue FrozenError
      true
    end
    first = up.read(5)
    up.rewind
    out_read = begin
      out = +'preset'
      result = up.read(2, out)
      [result, out, result.equal?(out), out.encoding.name]
    end
    up.rewind
    save_path = File.expand_path('../ai-tmp/upload-save-test.bin', __dir__)
    saved = up.save(save_path)
    saved_content = File.binread(save_path)
    File.delete(save_path)
    {
      'class'         => up.class.name,
      'is_upload'     => up.is_a?(Iodine::Base::Upload),
      'not_hash'      => !up.is_a?(Hash),
      'filename'      => up.filename,
      'type'          => up.type,
      'name'          => up.name,
      'headers'       => (up.headers.is_a?(String) && up.headers.include?('X-Extra: kept')),
      'tempfile_self' => up[:tempfile].equal?(up),
      'rack_filename' => (up[:filename] == up.filename),
      'rack_head'     => (up[:head] == up.headers),
      'rack_view_set' => ((up[:custom] = 'x') && up[:custom] == 'x'),
      'size'          => up.size,
      'frozen_out'    => frozen_error,
      'first5'        => first,
      'out_read'      => out_read,
      'read0'         => up.read(0),
      'rewound'       => (up.rewind == 0),
      'content'       => up.read,
      'eof_nil'       => up.read(1).nil?,
      'eof_empty'     => (up.read == ''),
      'out_at_eof'    => begin
        out = +'unchanged'
        result = up.read(1, out)
        [result, out]
      end,
      'out_zero'      => begin
        out = +'unchanged'
        result = up.read(0, out)
        [result, out, result.equal?(out)]
      end,
      'out_nil'       => begin
        up.rewind
        out = +'preset'
        result = up.read(nil, out)
        [result, out, result.equal?(out), out.encoding.name]
      end,
      'out_nil_eof'   => begin
        out = +'preset'
        result = up.read(nil, out)
        [result, out, result.equal?(out), out.encoding.name]
      end,
      'saved'         => saved,
      'saved_content' => saved_content,
      'closed_after'  => (up.close.nil? && up.closed? && up.close.nil?),
      'ioerror_after_close' => begin
        up.read
        false
      rescue IOError
        true
      end,
    }
  end

  def upload_race_report(up)
    base = File.expand_path('../ai-tmp/upload-race', __dir__)
    save_path = "#{base}-save.bin"
    race_path = "#{base}-close.bin"
    threads = []
    size = up.size
    saved = first = race = nil
    Timeout.timeout(10) do
      threads << Thread.new { up.save(save_path) }
      threads << Thread.new { up.rewind && up.read(64) }
      saved, first = threads.map(&:value)
      threads << Thread.new do
        up.save(race_path)
        'saved'
      rescue IOError
        'closed'
      end
      Thread.pass
      up.close
      race = threads.last.value
    end
    { 'size' => size, 'saved' => saved, 'read_ok' => first == ('x' * 64),
      'race' => race, 'closed' => up.closed?,
      'save_ok' => File.size(save_path) == size,
      'race_save_ok' => race == 'closed' || File.size(race_path) == size }
  ensure
    threads&.each { |thread| thread.kill if thread.alive? }
    threads&.each(&:join)
    [save_path, race_path].each do |path|
      File.delete(path) if path && File.exist?(path)
    end
  end

  def escape_uploads(group, uploads)
    uploads.each do |up|
      up[:escaped] = 'kept'
      PARAMS_ESCAPED_UPLOADS[group] << up
    end
  end

  def gc_enabled_snapshot
    enabled = !GC.disable # returns the previous disabled state
    GC.enable if enabled
    enabled
  end

  def with_gc_state(enabled)
    previous = gc_enabled_snapshot
    enabled ? GC.enable : GC.disable
    yield
  ensure
    previous ? GC.enable : GC.disable
  end

  def on_http(e)
    case e.path
    when '/params'
      json(e, e.params)
    when '/split' # body2params first: merge order follows the calls
      e.body2params
      e.query2params
      json(e, e.params)
    when '/memo'
      a = e.params
      b = e.query2params
      c = e.body2params
      json(e, { 'same' => a.equal?(b) && a.equal?(c), 'params' => a })
    when '/memo_query'
      a = e.query2params
      b = e.params
      before_body = b.dup
      e.query2params
      e.body2params
      e.body2params
      json(e, { 'same' => a.equal?(b) && a.equal?(e.params),
                'before_body' => before_body, 'params' => a })
    when '/memo_body'
      a = e.body2params
      b = e.params
      before_query = b.dup
      e.body2params
      e.query2params
      e.query2params
      json(e, { 'same' => a.equal?(b) && a.equal?(e.params),
                'before_query' => before_query, 'params' => a })
    when '/hash_subclass'
      params = e.query2params
      params['a'] = PARAMS_HASH_SUBCLASS.new.merge!('x' => 1)
      e.body2params
      merged = params['a']
      json(e, { 'subclass' => merged.class == PARAMS_HASH_SUBCLASS,
                'x' => merged['x'], 'y' => merged['y'],
                'has_empty' => merged.key?('') })
    when '/callback_frozen'
      params = e.body2params
      params['a'].freeze
      rescued = false
      begin
        e.query2params
      rescue FrozenError
        rescued = true
      end
      json(e, { 'rescued' => rescued, 'a' => params['a'] })
    when '/count'
      params = e.params
      json(e, { 'count' => params.size, 'has_json' => params.key?('json') })
    when '/frozen_body'
      with_gc_state(true) do
        params = e.query2params
        params.freeze
        position = e.seek(2)
        rescued = false
        begin
          e.body2params
        rescue FrozenError
          rescued = true
        end
        json(e, { 'rescued' => rescued, 'gc_enabled' => gc_enabled_snapshot,
                  'position' => position, 'remaining' => e.read })
      end
    when '/gc_enabled', '/gc_disabled'
      enabled = e.path == '/gc_enabled'
      with_gc_state(enabled) do
        before = gc_enabled_snapshot
        params = e.params
        json(e, { 'before' => before, 'after' => gc_enabled_snapshot,
                  'value' => params['x'] })
      end
    when '/read_after'
      e.body2params
      e.seek(0)
      json(e, { 'params' => e.params, 'body' => e.read })
    when '/closed'
      e.close_body
      json(e, e.body2params)
    when '/upload'
      up = e.params['file']
      json(e, up ? upload_report(up) : { 'error' => 'no upload', 'params' => e.params })
    when '/upload_conflict'
      v = e.params['file']
      if v.is_a?(Array)
        json(e, { 'array' => v.map { |x| x.is_a?(Iodine::Base::Upload) ? "Upload:#{x.filename}" : x } })
      else
        json(e, { 'single' => v.class.name })
      end
    when '/upload_big' # >= 256KiB: save runs outside the GVL
      up = e.params['big']
      save_path = File.expand_path('../ai-tmp/upload-big-test.bin', __dir__).freeze
      prefix = up.read(17)
      saved = up.save(save_path)
      saved_content = File.binread(save_path)
      File.delete(save_path)
      remaining = up.read
      json(e, { 'size' => up.size, 'saved' => saved,
                'content_ok' => saved_content == ('x' * 300_000),
                'position_ok' => prefix == ('x' * 17) && remaining == ('x' * (300_000 - 17)) })
    when '/upload_race'
      json(e, upload_race_report(e.params['big']))
    when '/cleanup'
      $params_saved_stream = e.params['file'][:tempfile]
      json(e, { 'open_during_request' => !$params_saved_stream.closed? })
    when '/cleanup_clear'
      params = e.params
      up = params['file']
      escape_uploads(:clear, [up])
      params.clear
      json(e, { 'open' => !up.closed?, 'empty' => params.empty? })
    when '/cleanup_removed'
      params = e.params
      uploads = [params['deleted'], params['replaced']]
      escape_uploads(:removed, uploads)
      params.delete('deleted')
      params['replaced'] = 'replacement'
      json(e, { 'open' => uploads.none?(&:closed?),
                'deleted' => !params.key?('deleted'),
                'replaced' => params['replaced'] == 'replacement' })
    when '/cleanup_multiple'
      params = e.params
      uploads = [params['first'], params['second']]
      escape_uploads(:multiple, uploads)
      uploads.first.close
      json(e, { 'early_closed' => uploads.first.closed?,
                'survivor_open' => !uploads.last.closed? })
    when '/cleanup_gc'
      params = e.params
      escaped = params['escaped']
      escape_uploads(:gc, [escaped])
      params.delete('discarded')
      params = nil
      GC.start(full_mark: true, immediate_sweep: true)
      json(e, { 'escaped_open' => !escaped.closed? })
    else
      e.status = 404
      e.finish('unknown')
    end
  end
end

# The exact 4,096-field query is ~15KiB; allow it past the 8KiB HTTP default.
Iodine.listen(url: "http://127.0.0.1:#{PARAMS_PORT}", handler: ParamsHandler,
              max_line_len: 32 * 1024)

RSpec.describe 'Iodine params extension' do
  PARAMS_STARTED = [false] # rubocop:disable RSpec/LeakyConstantDeclaration

  # Builds a multipart/form-data body with the given parts.
  # parts: [{ name:, value: } or { name:, filename:, type:, value: }]
  def multipart_body(parts, boundary)
    body = +''.b
    parts.each do |p|
      body << "--#{boundary}\r\n"
      if p[:filename]
        body << "Content-Disposition: form-data; name=\"#{p[:name]}\"; filename=\"#{p[:filename]}\"\r\n"
        body << "Content-Type: #{p[:type]}\r\n" if p[:type]
        body << "X-Extra: kept\r\n"
      else
        body << "Content-Disposition: form-data; name=\"#{p[:name]}\"\r\n"
      end
      body << "\r\n#{p[:value]}\r\n"
    end
    body << "--#{boundary}--\r\n"
    body
  end

  def upload_part(name, filename, value = 'data')
    { name: name, filename: filename, type: 'application/octet-stream', value: value }
  end

  def closed_upload_report(up)
    read_raises = begin
      up.read
      false
    rescue IOError
      true
    end
    up[:after_close] = 'usable'
    { 'closed' => up.closed?, 'read_raises' => read_raises,
      'rack_override' => up[:escaped] == 'kept' && up[:after_close] == 'usable',
      'tempfile_self' => up[:tempfile].equal?(up) }
  end

  def expect_closed_uploads(group, count)
    reports = PARAMS_RESULTS.dig(:cleanup_after, group)
    expect(reports.length).to eq(count)
    reports.each do |report|
      expect(report).to include('closed' => true, 'read_raises' => true,
                                'rack_override' => true, 'tempfile_self' => true)
    end
  end

  before(:context) do
    Iodine.workers = 0
    Iodine.threads = 4

    params_finished = false
    port = PARAMS_PORT
    run_tests = proc do
      base = "http://127.0.0.1:#{port}"
      get = ->(path) { JSON.parse(Net::HTTP.get_response(URI("#{base}#{path}")).body) }
      # raw path getter that skips URI percent-escape validation
      get_raw = lambda do |path|
        Net::HTTP.start('127.0.0.1', port) { |h| JSON.parse(h.get(path).body) }
      end
      post = lambda do |path, body, type|
        Net::HTTP.post(URI("#{base}#{path}"), body, 'Content-Type' => type)
      end
      postj = ->(path, body, type) { JSON.parse(post.call(path, body, type).body) }

      PARAMS_RESULTS[:basic]    = get.call('/params?a=1&b=2')
      PARAMS_RESULTS[:paths]    = get.call('/params?user[name]=A&user.age=3&tag[]=x&tag[]=y')
      PARAMS_RESULTS[:literal]  = get.call('/params?a..b=1&items[][id]=2&items[id][]=3')
      PARAMS_RESULTS[:dups]     = get.call('/params?a=1&a=2&a=3')
      PARAMS_RESULTS[:conflict1] = get.call('/params?user=x&user.name=A')
      PARAMS_RESULTS[:conflict2] = get.call('/params?user.name=A&user=x')
      PARAMS_RESULTS[:conflict3] = get.call('/params?tag=x&tag[]=a')
      PARAMS_RESULTS[:malformed] = get_raw.call('/params?a=%GG&flag&x=a+b&&y==1')
      PARAMS_RESULTS[:empty_q]   = get.call('/params')
      PARAMS_RESULTS[:memo]      = get.call('/memo?a=1')
      PARAMS_RESULTS[:memo_query] = postj.call('/memo_query?a=query', 'a=body&b=2',
                                               'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:memo_body] = postj.call('/memo_body?a=query', 'a=body&b=2',
                                              'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:form]      = postj.call('/params?a=query', 'a=body&b=2', 'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:split]     = postj.call('/split?a=query', 'a=body', 'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:frozen_body] = postj.call('/frozen_body?q=1', 'x=body&y=2',
                                                'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:gc_enabled] = get.call('/gc_enabled?x=1')
      PARAMS_RESULTS[:gc_disabled] = get.call('/gc_disabled?x=1')
      PARAMS_RESULTS[:read_after] = postj.call('/read_after', 'x=1', 'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:closed]    = postj.call('/closed', 'x=1', 'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:plain]     = postj.call('/params', 'a=1', 'text/plain')
      PARAMS_RESULTS[:no_ct]     = JSON.parse(Net::HTTP.post(URI("#{base}/params"), 'a=1').body)
      PARAMS_RESULTS[:json]      = postj.call('/params?q=1', '{"a":1,"b":{"c":[true,null]},"s":"txt"}', 'application/json')
      PARAMS_RESULTS[:json_suffix] = postj.call('/params', '{"ld":1}', 'application/ld+JSON; charset=utf-8')
      PARAMS_RESULTS[:json_long_suffix] = postj.call('/params', '{"long":1}',
                                                     "application/#{'a' * 300}+json")
      PARAMS_RESULTS[:json_arr]  = postj.call('/params', '[1,2,3]', 'application/json')
      PARAMS_RESULTS[:json_conflict] = postj.call('/params?a=query', '{"a":2}', 'application/json')
      PARAMS_RESULTS[:hash_subclass] = postj.call('/hash_subclass', '{"a":{"y":2}}',
                                                  'application/json')
      PARAMS_RESULTS[:callback_frozen] = postj.call('/callback_frozen?a[y]=2', '{"a":{"x":1}}',
                                                    'application/json')
      PARAMS_RESULTS[:json_false_after] = postj.call('/params', '{"a":1,"a":false}', 'application/json')
      PARAMS_RESULTS[:json_false_before] = postj.call('/params', '{"a":false,"a":1}', 'application/json')
      PARAMS_RESULTS[:json_false_array] = postj.call('/params', '{"a":[1],"a":false}', 'application/json')
      PARAMS_RESULTS[:json_false_hash] = postj.call('/params', '{"a":{"x":1},"a":false}', 'application/json')
      PARAMS_RESULTS[:json_false_query_first] = postj.call('/params?a=query', '{"a":false}', 'application/json')
      PARAMS_RESULTS[:json_false_body_first] = postj.call('/split?a=query', '{"a":false}', 'application/json')
      PARAMS_RESULTS[:json_incomplete_hash] = postj.call('/params', '{"ok":1,"bad":{"nested":2', 'application/json')
      PARAMS_RESULTS[:json_incomplete_array] = postj.call('/params', '{"ok":1,"bad":[1,2', 'application/json')
      PARAMS_RESULTS[:json_missing_value] = postj.call('/params', '{"ok":1,"bad":}', 'application/json')
      spilled_json = '{"ok":"' + ('x' * 200_000) + '","bad":123'
      PARAMS_RESULTS[:json_spilled] = postj.call('/params', spilled_json, 'application/json')
      number_json = '{"ok":1,"bad":123'
      PARAMS_RESULTS[:json_number_position] = postj.call('/read_after', number_json,
                                                         'application/json')

      # multipart: text field + file upload
      boundary = 'iodineparamstest'
      mp = multipart_body([{ name: 'text', value: 'hello' },
                           { name: 'file', filename: 'note.txt', type: 'text/plain', value: 'file-content-here' }],
                          boundary)
      PARAMS_RESULTS[:upload] = postj.call('/upload', mp, "multipart/form-data; boundary=#{boundary}")

      # upload conflicting with a text field of the same name
      mp2 = multipart_body([{ name: 'file', value: 'plain' },
                            { name: 'file', filename: 'a.txt', type: 'text/plain', value: 'abc' }],
                           boundary)
      PARAMS_RESULTS[:upload_conflict] = postj.call('/upload_conflict', mp2, "multipart/form-data; boundary=#{boundary}")

      # upload stream cleanup at request end
      multipart_type = "multipart/form-data; boundary=#{boundary}"
      PARAMS_RESULTS[:cleanup] = postj.call('/cleanup', mp, multipart_type)

      clear_mp = multipart_body([upload_part('file', 'clear.txt')], boundary)
      PARAMS_RESULTS[:cleanup_clear] = postj.call('/cleanup_clear', clear_mp, multipart_type)
      removed_mp = multipart_body([upload_part('deleted', 'deleted.txt'),
                                   upload_part('replaced', 'replaced.txt')], boundary)
      PARAMS_RESULTS[:cleanup_removed] = postj.call('/cleanup_removed', removed_mp, multipart_type)
      multiple_mp = multipart_body([upload_part('first', 'first.txt'),
                                    upload_part('second', 'second.txt')], boundary)
      PARAMS_RESULTS[:cleanup_multiple] = postj.call('/cleanup_multiple', multiple_mp, multipart_type)
      gc_mp = multipart_body([upload_part('escaped', 'escaped.txt'),
                              upload_part('discarded', 'discarded.txt')], boundary)
      PARAMS_RESULTS[:cleanup_gc] = postj.call('/cleanup_gc', gc_mp, multipart_type)

      # large upload (300,000 bytes > 256KiB): save runs outside the GVL
      mp3 = multipart_body([{ name: 'big', filename: 'big.bin', type: 'application/octet-stream',
                              value: 'x' * 300_000 }],
                           boundary)
      PARAMS_RESULTS[:upload_big] = postj.call('/upload_big', mp3, multipart_type)
      PARAMS_RESULTS[:upload_race] = postj.call('/upload_race', mp3, multipart_type)

      truncated = multipart_body([{ name: 'text', value: 'hello' },
                                  upload_part('file', 'truncated.txt')], boundary)
      closing = "--#{boundary}--\r\n"
      truncated = truncated.byteslice(0, truncated.bytesize - closing.bytesize)
      PARAMS_RESULTS[:multipart_truncated] = postj.call('/params', truncated, multipart_type)

      # The shared query/form budget is exact; JSON malformation is not overflow.
      # Compact unique keys keep the request target below HTTP line limits.
      query_at_limit = (0...4096).map { |i| i.to_s(36) }.join('&')
      PARAMS_RESULTS[:limit_exact] = postj.call("/count?#{query_at_limit}", '&&&',
                                                'application/x-www-form-urlencoded')
      PARAMS_RESULTS[:limit_json] = postj.call("/count?#{query_at_limit}", '{"json":}',
                                               'application/json')

      # 4097 actual form fields must still be rejected (RangeError -> 500).
      big = (0...4097).map { |i| "#{i}=" }.join('&')
      PARAMS_RESULTS[:limit_status] = post.call('/params', big, 'application/x-www-form-urlencoded').code.to_i

      deep_name = 'a' + ('[a]' * 128) # 129 segments; the native bound is 128.
      PARAMS_RESULTS[:depth_status] = post.call('/params', "#{deep_name}=1",
                                                'application/x-www-form-urlencoded').code.to_i
      deep_text = multipart_body([{ name: deep_name, value: 'text' }], boundary)
      PARAMS_RESULTS[:depth_multipart_text_status] =
        post.call('/params', deep_text, multipart_type).code.to_i
      deep_file = multipart_body([upload_part(deep_name, 'deep.txt')], boundary)
      PARAMS_RESULTS[:depth_multipart_file_status] =
        post.call('/params', deep_file, multipart_type).code.to_i

      # Omitted multipart parts still count against the cross-source budget.
      omitted = multipart_body([upload_part('empty', ''), upload_part('', 'named.txt')], boundary)
      query_one_short = (0...4095).map { |i| i.to_s(36) }.join('&')
      PARAMS_RESULTS[:limit_omitted] = post.call("/params?#{query_one_short}", omitted,
                                                 multipart_type).code.to_i
    rescue => e
      PARAMS_RESULTS[:error] = "#{e.class}: #{e.message}\n#{e.backtrace.first(3).join("\n")}\ncompleted: #{PARAMS_RESULTS.keys.inspect}"
    ensure
      params_finished = true
      Iodine.run_after(100) { Iodine.stop }
    end

    Iodine.on_state(:start) do
      next if PARAMS_STARTED[0]
      PARAMS_STARTED[0] = true
      Iodine.run_after(100) { Iodine.async { run_tests.call } }
    end

    Iodine.start

    # uploads must be closed (and temp storage removed) at request end
    PARAMS_RESULTS[:cleanup_closed_after] = $params_saved_stream ? $params_saved_stream.closed? : nil
    PARAMS_RESULTS[:cleanup_after] = PARAMS_ESCAPED_UPLOADS.transform_values do |uploads|
      uploads.map { |up| closed_upload_report(up) }
    end
    raise "params tests did not complete: #{PARAMS_RESULTS[:error]}" unless params_finished && !PARAMS_RESULTS[:error]
  end

  it 'parses simple query pairs' do
    expect(PARAMS_RESULTS[:basic]).to eq('a' => '1', 'b' => '2')
  end

  it 'builds nested Hashes from bracket and dot paths, arrays from terminal []' do
    expect(PARAMS_RESULTS[:paths]).to eq('user' => { 'name' => 'A', 'age' => '3' },
                                         'tag' => %w[x y])
  end

  it 'keeps invalid paths literal' do
    expect(PARAMS_RESULTS[:literal]).to eq('a..b' => '1', 'items[][id]' => '2',
                                           'items' => { 'id' => ['3'] })
  end

  it 'collects repeated scalars into an Array in arrival order' do
    expect(PARAMS_RESULTS[:dups]).to eq('a' => %w[1 2 3])
  end

  it 'nests a scalar conflicting with a map under the "" key (either order)' do
    expect(PARAMS_RESULTS[:conflict1]).to eq('user' => { '' => 'x', 'name' => 'A' })
    expect(PARAMS_RESULTS[:conflict2]).to eq('user' => { 'name' => 'A', '' => 'x' })
  end

  it 'appends [] values to an existing scalar as an Array' do
    expect(PARAMS_RESULTS[:conflict3]).to eq('tag' => %w[x a])
  end

  it 'never raises on malformed input: literal escapes, flag keys, skipped empty segments' do
    expect(PARAMS_RESULTS[:malformed]).to eq('a' => '%GG', 'flag' => '', 'x' => 'a b', 'y' => '=1')
  end

  it 'treats a missing query as no parameters' do
    expect(PARAMS_RESULTS[:empty_q]).to eq({})
  end

  it 'returns the same memoized Hash from params, query2params and body2params' do
    expect(PARAMS_RESULTS[:memo]['same']).to be(true)
    expect(PARAMS_RESULTS[:memo]['params']).to eq('a' => '1')
  end

  it 'returns query memoization without parsing the body, then merges each source once' do
    expect(PARAMS_RESULTS[:memo_query]).to eq(
      'same' => true, 'before_body' => { 'a' => 'query' },
      'params' => { 'a' => %w[query body], 'b' => '2' }
    )
  end

  it 'returns body memoization without parsing the query, then merges each source once' do
    expect(PARAMS_RESULTS[:memo_body]).to eq(
      'same' => true, 'before_query' => { 'a' => 'body', 'b' => '2' },
      'params' => { 'a' => %w[body query], 'b' => '2' }
    )
  end

  it 'merges form body after the query, keeping both values' do
    expect(PARAMS_RESULTS[:form]).to eq('a' => %w[query body], 'b' => '2')
  end

  it 'honors explicit call order (body2params before query2params)' do
    expect(PARAMS_RESULTS[:split]).to eq('a' => %w[body query])
  end

  it 'preserves the body reading position (e.read sees the same bytes)' do
    expect(PARAMS_RESULTS[:read_after]).to eq('params' => { 'x' => '1' }, 'body' => 'x=1')
  end

  it 'restores the body position after a rescued FrozenError during body parsing' do
    r = PARAMS_RESULTS[:frozen_body]
    expect(r['rescued']).to be(true)
    expect(r['position']).to eq(2)
    expect(r['remaining']).to eq('body&y=2')
  end

  it 'restores GC state after a rescued FrozenError when GC was initially enabled' do
    expect(PARAMS_RESULTS[:frozen_body]['gc_enabled']).to be(true)
  end

  it 'preserves enabled and pre-disabled GC states after successful parsing' do
    expect(PARAMS_RESULTS[:gc_enabled]).to eq(
      'before' => true, 'after' => true, 'value' => '1'
    )
    expect(PARAMS_RESULTS[:gc_disabled]).to eq(
      'before' => false, 'after' => false, 'value' => '1'
    )
  end

  it 'parses nothing from a closed body' do
    expect(PARAMS_RESULTS[:closed]).to eq({})
  end

  it 'never parses text/plain' do
    expect(PARAMS_RESULTS[:plain]).to eq({})
  end

  it 'never parses a body without a Content-Type' do
    expect(PARAMS_RESULTS[:no_ct]).to eq({})
  end

  it 'parses JSON bodies, keeping Ruby types and merging with the query' do
    expect(PARAMS_RESULTS[:json]).to eq('q' => '1', 'a' => 1, 's' => 'txt',
                                        'b' => { 'c' => [true, nil] })
  end

  it 'parses application/<subtype>+json (case and parameters ignored)' do
    expect(PARAMS_RESULTS[:json_suffix]).to eq('ld' => 1)
    expect(PARAMS_RESULTS[:json_long_suffix]).to eq('long' => 1)
  end

  it 'ignores a non-object JSON root' do
    expect(PARAMS_RESULTS[:json_arr]).to eq({})
  end

  it 'merges JSON object members with conflict resolution (query first)' do
    expect(PARAMS_RESULTS[:json_conflict]).to eq('a' => ['query', 2])
  end

  it 'recursively merges app-seeded Hash subclasses' do
    expect(PARAMS_RESULTS[:hash_subclass]).to eq(
      'subclass' => true, 'x' => 1, 'y' => 2, 'has_empty' => false
    )
  end

  it 're-raises callback exceptions after native parser cleanup' do
    expect(PARAMS_RESULTS[:callback_frozen]).to eq(
      'rescued' => true, 'a' => { 'x' => 1 }
    )
  end

  it 'preserves false in duplicate and container conflicts' do
    expect(PARAMS_RESULTS[:json_false_after]).to eq('a' => [1, false])
    expect(PARAMS_RESULTS[:json_false_before]).to eq('a' => [false, 1])
    expect(PARAMS_RESULTS[:json_false_array]).to eq('a' => [1, false])
    expect(PARAMS_RESULTS[:json_false_hash]).to eq('a' => { 'x' => 1, '' => false })
  end

  it 'preserves JSON false on either side of a query scalar conflict' do
    expect(PARAMS_RESULTS[:json_false_query_first]).to eq('a' => ['query', false])
    expect(PARAMS_RESULTS[:json_false_body_first]).to eq('a' => [false, 'query'])
  end

  it 'does not raise or retain incomplete trailing JSON Hash and Array members' do
    [PARAMS_RESULTS[:json_incomplete_hash], PARAMS_RESULTS[:json_incomplete_array]].each do |params|
      expect(params).to satisfy { |value| value == {} || value == { 'ok' => 1 } }
    end
  end

  it 'does not raise or invent false for a missing JSON value' do
    params = PARAMS_RESULTS[:json_missing_value]
    expect(params).to satisfy { |value| value == {} || value == { 'ok' => 1 } }
  end

  it 'discards malformed spilled JSON ending in a number and restores position' do
    expect(PARAMS_RESULTS[:json_spilled]).to eq({})
    expect(PARAMS_RESULTS[:json_number_position]).to eq(
      'params' => {}, 'body' => '{"ok":1,"bad":123'
    )
  end

  it 'parses multipart text fields and file uploads' do
    up = PARAMS_RESULTS[:upload]
    expect(up['error']).to be_nil
    expect(up['is_upload']).to be(true)
    expect(up['not_hash']).to be(true)
    expect(up['filename']).to eq('note.txt')
    expect(up['type']).to eq('text/plain')
    expect(up['name']).to eq('file')
    expect(up['headers']).to be(true)
    expect(up['tempfile_self']).to be(true)
    expect(up['rack_filename']).to be(true)
    expect(up['rack_head']).to be(true)
    expect(up['rack_view_set']).to be(true)
  end

  it 'provides the NeoRack Upload read/rewind/size/save/close contract' do
    up = PARAMS_RESULTS[:upload]
    expect(up['size']).to eq('file-content-here'.bytesize)
    expect(up['frozen_out']).to be(true)
    expect(up['first5']).to eq('file-')
    expect(up['out_read']).to eq(['fi', 'fi', true, 'UTF-8'])
    expect(up['rewound']).to be(true)
    expect(up['read0']).to eq('')
    expect(up['content']).to eq('file-content-here')
    expect(up['eof_nil']).to be(true)
    expect(up['eof_empty']).to be(true)
    expect(up['out_at_eof']).to eq([nil, ''])
    expect(up['out_zero']).to eq(['', '', true])
    expect(up['out_nil']).to eq(['file-content-here', 'file-content-here', true, 'ASCII-8BIT'])
    expect(up['out_nil_eof']).to eq(['', '', true, 'ASCII-8BIT'])
    expect(up['saved']).to eq('file-content-here'.bytesize)
    expect(up['saved_content']).to eq('file-content-here')
    expect(up['closed_after']).to be(true)
    expect(up['ioerror_after_close']).to be(true)
  end

  it 'treats uploads as scalar values in conflicts' do
    expect(PARAMS_RESULTS[:upload_conflict]).to eq('array' => ['plain', 'Upload:a.txt'])
  end

  it 'discards a truncated multipart stage without exposing its partial upload' do
    expect(PARAMS_RESULTS[:multipart_truncated]).to eq({})
  end

  it 'keeps uploads open during the request and closes them at request end' do
    expect(PARAMS_RESULTS[:cleanup]['open_during_request']).to be(true)
    expect(PARAMS_RESULTS[:cleanup_closed_after]).to be(true)
  end

  it 'closes escaped uploads after params.clear and keeps their Rack view usable' do
    expect(PARAMS_RESULTS[:cleanup_clear]).to eq('open' => true, 'empty' => true)
    expect_closed_uploads(:clear, 1)
  end

  it 'closes escaped uploads after deletion and replacement' do
    expect(PARAMS_RESULTS[:cleanup_removed]).to eq(
      'open' => true, 'deleted' => true, 'replaced' => true
    )
    expect_closed_uploads(:removed, 2)
  end

  it 'closes all escaped uploads when one was manually closed early' do
    expect(PARAMS_RESULTS[:cleanup_multiple]).to eq(
      'early_closed' => true, 'survivor_open' => true
    )
    expect_closed_uploads(:multiple, 2)
  end

  it 'survives early GC after an upload loses its params reference' do
    expect(PARAMS_RESULTS[:cleanup_gc]).to eq('escaped_open' => true)
    expect_closed_uploads(:gc, 1)
  end

  it 'accepts exactly 4096 query fields followed by empty form segments' do
    expect(PARAMS_RESULTS[:limit_exact]).to eq('count' => 4096, 'has_json' => false)
  end

  it 'does not report malformed JSON as overflow after exactly 4096 query fields' do
    expect(PARAMS_RESULTS[:limit_json]).to eq('count' => 4096, 'has_json' => false)
  end

  it 'rejects requests exceeding the field limit (RangeError -> 500)' do
    expect(PARAMS_RESULTS[:limit_status]).to eq(500)
    expect(PARAMS_RESULTS[:limit_omitted]).to eq(500)
  end

  it 'rejects URL-encoded names exceeding the native nesting bound' do
    expect(PARAMS_RESULTS[:depth_status]).to eq(500)
  end

  it 'rejects multipart text and file names exceeding the native nesting bound' do
    expect(PARAMS_RESULTS[:depth_multipart_text_status]).to eq(500)
    expect(PARAMS_RESULTS[:depth_multipart_file_status]).to eq(500)
  end

  it 'saves large uploads (256KiB+, saved outside the GVL)' do
    r = PARAMS_RESULTS[:upload_big]
    expect(r['size']).to eq(300_000)
    expect(r['saved']).to eq(300_000)
    expect(r['content_ok']).to be(true)
    expect(r['position_ok']).to be(true)
  end

  it 'serializes large saves with reads and close without deadlocking' do
    r = PARAMS_RESULTS[:upload_race]
    expect(r['saved']).to eq(300_000)
    expect(r['read_ok']).to be(true)
    expect(r['race']).to match(/\A(?:saved|closed)\z/)
    expect(r['closed']).to be(true)
    expect(r['save_ok']).to be(true)
    expect(r['race_save_ok']).to be(true)
  end

  describe Iodine::Base::Upload do
    def seed_upload_gc_view(up)
      lookups = %w[first second].map do |name|
        key = String.new("#{name}-key")
        lookup = key.dup
        up[key] = String.new("#{name}-value")
        key << '-changed' # MiniMap retains a duplicated, frozen key.
        lookup
      end
      up[:head] = false
      up[:tempfile] = nil
      up[:self] = up
      lookups
    end

    it 'marks its embedded Rack view through major GC and compaction, including after close' do
      up = described_class.new
      lookups = seed_upload_gc_view(up)
      GC.start(full_mark: true, immediate_sweep: true)
      GC.compact if GC.respond_to?(:compact)
      expect(lookups.map { |key| up[key] }).to eq(%w[first-value second-value])
      expect(up[:head]).to be(false)
      expect(up[:tempfile]).to be_nil
      expect(up[:self]).to equal(up)
      expect([up.filename, up.type, up.name, up.headers]).to eq([nil, nil, nil, nil])
      up.close
      up[lookups.first] = 'after-close'
      expect(up[lookups.first]).to eq('after-close')
      expect(up[lookups.last]).to eq('second-value')
      expect(up[:self]).to equal(up)
      expect { up.read }.to raise_error(IOError)
    end

    it 'keeps required-key overrides in the Rack compatibility view only' do
      up = described_class.new
      expect(up[:tempfile]).to equal(up)
      up[:filename] = 'view.txt'
      up[:head] = false
      up[:tempfile] = nil
      expect(up[:filename]).to eq('view.txt')
      expect(up[:head]).to be(false)
      expect(up[:tempfile]).to be_nil
      expect(up.filename).to be_nil
      expect(up.headers).to be_nil
      up.close
    end

    it 'supports custom keys with ordinary Hash semantics' do
      up = described_class.new
      object_key = Object.new
      expect(up[object_key]).to be_nil
      up[:custom] = false
      up['custom'] = 'string'
      up[object_key] = false
      GC.start(full_mark: true, immediate_sweep: true)
      GC.compact if GC.respond_to?(:compact)
      expect(up[:custom]).to be(false)
      expect(up['custom']).to eq('string')
      expect(up[:missing]).to be_nil
      expect(up[object_key]).to be(false)
      up.close
    end

    it 'accepts String save paths only, like Ruby\'s own IO (no Symbols)' do
      up = Iodine::Base::Upload.new
      base = File.expand_path('../ai-tmp', __dir__)
      expect { up.save(12_345) }.to raise_error(TypeError)
      expect { up.save(Object.new) }.to raise_error(TypeError)
      expect { up.save(:"#{base}/u2.bin") }.to raise_error(TypeError)
      expect { up.save("nul\0byte") }.to raise_error(ArgumentError)
      expect(up.save(File.join(base, 'u1.bin'))).to eq(0)
      File.delete(File.join(base, 'u1.bin'))
      up.close
      expect { up.save(File.join(base, 'u3.bin')) }.to raise_error(IOError)
    end
  end
end
