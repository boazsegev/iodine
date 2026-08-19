# frozen_string_literal: true

require 'spec_helper'

RSpec.describe Iodine::JSON do
  before do
    skip 'Iodine::JSON not defined' unless defined?(Iodine::JSON)
  end

  describe '.stringify' do
    it 'returns a String' do
      expect(Iodine::JSON.stringify({ key: 'value' })).to be_a(String)
    end

    it 'serializes a simple hash' do
      result = Iodine::JSON.stringify({ key: 'value' })
      expect(result).to include('value')
    end

    it 'serializes nil as "null"' do
      expect(Iodine::JSON.stringify(nil)).to eq('null')
    end

    it 'serializes true as "true"' do
      expect(Iodine::JSON.stringify(true)).to eq('true')
    end

    it 'serializes false as "false"' do
      expect(Iodine::JSON.stringify(false)).to eq('false')
    end

    it 'serializes an Integer' do
      expect(Iodine::JSON.stringify(42)).to eq('42')
    end

    it 'serializes a Float' do
      result = Iodine::JSON.stringify(3.14)
      expect(result).to include('3.14').or include('3.1')
    end

    it 'serializes an Array' do
      result = Iodine::JSON.stringify([1, 2, 3])
      expect(result).to include('1').and include('2').and include('3')
    end

    it 'serializes a nested Hash' do
      result = Iodine::JSON.stringify({ outer: { inner: 'value' } })
      expect(result).to include('value')
    end

    it 'serializes an empty Hash' do
      result = Iodine::JSON.stringify({})
      expect(result).to eq('{}')
    end

    it 'serializes an empty Array' do
      result = Iodine::JSON.stringify([])
      expect(result).to eq('[]')
    end
  end

  describe '.parse' do
    it 'parses a JSON object string' do
      result = Iodine::JSON.parse('{"key":"value"}')
      expect(result).to be_a(Hash)
    end

    it 'parses a JSON array string' do
      result = Iodine::JSON.parse('[1,2,3]')
      expect(result).to be_a(Array)
    end

    it 'parses null as nil' do
      expect(Iodine::JSON.parse('null')).to be_nil
    end

    it 'parses true as true' do
      expect(Iodine::JSON.parse('true')).to be true
    end

    it 'parses false as false' do
      expect(Iodine::JSON.parse('false')).to be false
    end

    it 'parses an integer' do
      expect(Iodine::JSON.parse('42')).to eq(42)
    end
  end

  describe 'round-trip (stringify then parse)' do
    it 'round-trips a simple hash' do
      original = { 'key' => 'value' }
      json     = Iodine::JSON.stringify(original)
      result   = Iodine::JSON.parse(json)
      expect(result['key']).to eq('value')
    end

    it 'round-trips an array of integers' do
      original = [1, 2, 3, 4, 5]
      json     = Iodine::JSON.stringify(original)
      result   = Iodine::JSON.parse(json)
      expect(result).to eq(original)
    end

    it 'round-trips nil' do
      json   = Iodine::JSON.stringify(nil)
      result = Iodine::JSON.parse(json)
      expect(result).to be_nil
    end

    it 'round-trips nested structures' do
      original = { 'users' => [{ 'name' => 'Alice', 'age' => 30 }] }
      json     = Iodine::JSON.stringify(original)
      result   = Iodine::JSON.parse(json)
      expect(result['users'][0]['name']).to eq('Alice')
    end
  end

  describe '.beautify' do
    it 'returns a String' do
      expect(Iodine::JSON.beautify({ key: 'value' })).to be_a(String)
    end

    it 'returns a longer string than stringify (has whitespace)' do
      obj = { key: 'value', nested: { a: 1 } }
      compact   = Iodine::JSON.stringify(obj)
      beautiful = Iodine::JSON.beautify(obj)
      expect(beautiful.length).to be >= compact.length
    end

    it 'produces valid JSON that can be parsed back' do
      obj    = { 'name' => 'test', 'values' => [1, 2, 3] }
      pretty = Iodine::JSON.beautify(obj)
      result = Iodine::JSON.parse(pretty)
      expect(result['name']).to eq('test')
    end

    it 'serializes nil as "null"' do
      result = Iodine::JSON.beautify(nil)
      expect(result.strip).to eq('null')
    end
  end
end
