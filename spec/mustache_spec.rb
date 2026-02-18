# frozen_string_literal: true

require 'spec_helper'

RSpec.describe Iodine::Mustache do
  before do
    skip 'Iodine::Mustache not defined' unless defined?(Iodine::Mustache)
  end

  # ---------------------------------------------------------------------------
  # Class method .render
  # ---------------------------------------------------------------------------
  describe '.render' do
    it 'renders a simple variable substitution' do
      result = Iodine::Mustache.render(template: '{{name}}', ctx: { name: 'World' })
      expect(result).to eq('World')
    end

    it 'renders a template with multiple variables' do
      result = Iodine::Mustache.render(
        template: 'Hello, {{first}} {{last}}!',
        ctx: { first: 'John', last: 'Doe' }
      )
      expect(result).to eq('Hello, John Doe!')
    end

    it 'renders nil or empty string for missing keys (does not raise)' do
      result = Iodine::Mustache.render(template: '{{missing}}', ctx: {})
      # Iodine::Mustache returns nil when the entire output is empty
      expect(result.nil? || result == '').to be true
    end

    it 'does not raise for missing keys' do
      expect { Iodine::Mustache.render(template: '{{missing}}', ctx: {}) }.not_to raise_error
    end

    it 'renders static text without variables unchanged' do
      result = Iodine::Mustache.render(template: 'Hello, World!', ctx: {})
      expect(result).to eq('Hello, World!')
    end

    it 'returns a String' do
      result = Iodine::Mustache.render(template: '{{name}}', ctx: { name: 'test' })
      expect(result).to be_a(String)
    end

    context 'HTML escaping' do
      it 'escapes HTML in double-brace variables' do
        result = Iodine::Mustache.render(
          template: '{{content}}',
          ctx: { content: '<script>alert("xss")</script>' }
        )
        expect(result).not_to include('<script>')
      end

      it 'does not escape HTML in triple-brace variables' do
        result = Iodine::Mustache.render(
          template: '{{{content}}}',
          ctx: { content: '<b>bold</b>' }
        )
        expect(result).to include('<b>bold</b>')
      end

      it 'escapes < in double-brace output' do
        result = Iodine::Mustache.render(template: '{{val}}', ctx: { val: '<' })
        expect(result).not_to include('<')
        expect(result).to include('&lt;').or include('&#60;').or include('&#x3c;').or include('&#x3C;')
      end

      it 'escapes & in double-brace output' do
        result = Iodine::Mustache.render(template: '{{val}}', ctx: { val: '&' })
        expect(result).not_to eq('&')
        expect(result).to include('&amp;').or include('&#38;').or include('&#x26;')
      end
    end

    context 'sections ({{#section}})' do
      it 'renders a truthy section' do
        result = Iodine::Mustache.render(
          template: '{{#show}}visible{{/show}}',
          ctx: { show: true }
        )
        expect(result).to include('visible')
      end

      it 'does not render a falsy section' do
        result = Iodine::Mustache.render(
          template: '{{#show}}visible{{/show}}',
          ctx: { show: false }
        )
        # Returns nil or empty string when nothing is rendered
        expect(result.to_s).not_to include('visible')
      end

      it 'does not render a nil section' do
        result = Iodine::Mustache.render(
          template: '{{#show}}visible{{/show}}',
          ctx: { show: nil }
        )
        # Returns nil or empty string when nothing is rendered
        expect(result.to_s).not_to include('visible')
      end

      it 'iterates over an array section' do
        result = Iodine::Mustache.render(
          template: '{{#items}}{{name}} {{/items}}',
          ctx: { items: [{ name: 'Alice' }, { name: 'Bob' }] }
        )
        expect(result).to include('Alice')
        expect(result).to include('Bob')
      end
    end

    context 'inverted sections ({{^section}})' do
      it 'renders an inverted section when value is false' do
        result = Iodine::Mustache.render(
          template: '{{^show}}hidden{{/show}}',
          ctx: { show: false }
        )
        expect(result).to include('hidden')
      end

      it 'does not render an inverted section when value is true' do
        result = Iodine::Mustache.render(
          template: '{{^show}}hidden{{/show}}',
          ctx: { show: true }
        )
        # Returns nil or empty string when nothing is rendered
        expect(result.to_s).not_to include('hidden')
      end

      it 'renders an inverted section for missing key' do
        result = Iodine::Mustache.render(
          template: '{{^missing}}shown{{/missing}}',
          ctx: {}
        )
        expect(result).to include('shown')
      end
    end

    context 'dot notation' do
      it 'supports nested key access via dot notation' do
        result = Iodine::Mustache.render(
          template: '{{user.name}}',
          ctx: { user: { name: 'Alice' } }
        )
        expect(result).to eq('Alice')
      end
    end
  end

  # ---------------------------------------------------------------------------
  # Instance method #render
  # ---------------------------------------------------------------------------
  describe '#render (instance)' do
    it 'renders a template provided at initialization' do
      view = Iodine::Mustache.new(nil, '{{greeting}}, {{name}}!')
      result = view.render(greeting: 'Hello', name: 'World')
      expect(result).to eq('Hello, World!')
    end

    it 'can be reused with different contexts' do
      view = Iodine::Mustache.new(nil, '{{value}}')
      expect(view.render(value: 'first')).to eq('first')
      expect(view.render(value: 'second')).to eq('second')
    end

    it 'renders nil or empty string for missing keys' do
      view = Iodine::Mustache.new(nil, '{{missing}}')
      result = view.render({})
      # Iodine::Mustache returns nil when the entire output is empty
      expect(result.nil? || result == '').to be true
    end

    it 'returns a String' do
      view = Iodine::Mustache.new(nil, 'hello')
      expect(view.render({})).to be_a(String)
    end
  end
end
