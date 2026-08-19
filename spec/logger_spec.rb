# frozen_string_literal: true

require 'spec_helper'
require 'tempfile'

# =============================================================================
# Iodine::Logger Tests
#
# The Logger routes through the C STL logging (FIO_LOG_*), which writes to the
# process' C stderr. Capturing that output requires re-associating fd 2 with a
# temporary file via $stderr.reopen (Ruby's capture: on $stderr is not enough,
# since the C layer holds its own FILE*).
# =============================================================================
RSpec.describe Iodine::Logger do
  # Runs the block with C stderr redirected to a temp file, returns the
  # captured output with ANSI color codes stripped.
  def capture_log_output
    Tempfile.create('iodine-log') do |file|
      original = $stderr.dup
      begin
        $stderr.reopen(file)
        yield
      ensure
        $stderr.reopen(original)
        original.close
      end
      file.rewind
      return file.read.gsub(/\e\[[0-9;]*m/, '')
    end
  end

  # The log level is global process state (the C STL FIO_LOG_LEVEL) - always
  # restore the spec suite's level after each example.
  after(:each) do
    Iodine::Logger.level = TEST_VERBOSITY
  end

  describe 'level constants' do
    it 'mirror the facil.io FIO_LOG_LEVEL_* values' do
      expect(Iodine::Logger::NONE).to eq(0)
      expect(Iodine::Logger::FATAL).to eq(1)
      expect(Iodine::Logger::ERROR).to eq(2)
      expect(Iodine::Logger::WARN).to eq(3)
      expect(Iodine::Logger::INFO).to eq(4)
      expect(Iodine::Logger::DEBUG).to eq(5)
    end
  end

  describe 'level helpers' do
    it 'reads back the level that was set' do
      Iodine::Logger.level = :warn
      expect(Iodine::Logger.level).to eq(3)
      Iodine::Logger.level = 5
      expect(Iodine::Logger.level).to eq(5)
    end

    it 'accepts symbolic levels' do
      { none: 0, fatal: 1, error: 2, warn: 3, info: 4, debug: 5 }.each do |sym, level|
        Iodine::Logger.level = sym
        expect(Iodine::Logger.level).to eq(level)
      end
    end

    it 'rejects unknown symbols' do
      expect { Iodine::Logger.level = :bogus }.to raise_error(ArgumentError)
    end

    it 'rejects out-of-range integers' do
      expect { Iodine::Logger.level = 9 }.to raise_error(ArgumentError)
    end

    it 'rejects non-numeric levels' do
      expect { Iodine::Logger.level = 'debug' }.to raise_error(TypeError)
    end
  end

  describe 'leveled logging' do
    it 'routes messages through the C STL logging with level prefixes' do
      Iodine::Logger.level = :debug
      output = capture_log_output do
        Iodine::Logger.info 'info message'
        Iodine::Logger.warn 'warn message'
        Iodine::Logger.error 'error message'
        Iodine::Logger.fatal 'fatal message'
      end
      expect(output).to include('INFO:     info message')
      expect(output).to include('WARNING:  warn message')
      expect(output).to include('ERROR:    error message')
      expect(output).to include('FATAL:    fatal message')
    end

    it 'respects the verbosity level' do
      Iodine::Logger.level = :warn
      output = capture_log_output do
        Iodine::Logger.info 'hidden info'
        Iodine::Logger.debug 'hidden debug'
        Iodine::Logger.warn 'visible warn'
      end
      expect(output).not_to include('hidden info')
      expect(output).not_to include('hidden debug')
      expect(output).to include('visible warn')
    end

    it 'hides debug messages below the DEBUG verbosity' do
      Iodine::Logger.level = :info
      output = capture_log_output { Iodine::Logger.debug 'hidden at info' }
      expect(output).not_to include('hidden at info')
    end

    it 'converts non-String objects with #to_s' do
      Iodine::Logger.level = :info
      output = capture_log_output { Iodine::Logger.info 42, :symbol }
      expect(output).to include('42')
      expect(output).to include('symbol')
    end
  end

  describe 'variadic messages' do
    it 'logs each argument as its own message' do
      Iodine::Logger.level = :info
      output = capture_log_output { Iodine::Logger.debug 'Look at this:', 42 }
      expect(output).to be_empty # debug silenced below DEBUG verbosity

      Iodine::Logger.level = :debug
      output = capture_log_output { Iodine::Logger.debug 'Look at this:', 42 }
      expect(output).to include("DEBUG:    Look at this:\n")
      expect(output).to include("DEBUG:    42\n")
    end
  end

  describe 'callable and block messages (Rack SPEC compatible)' do
    it 'invokes arguments that answer #call and logs the return value' do
      Iodine::Logger.level = :info
      output = capture_log_output { Iodine::Logger.info -> { 'from callable' } }
      expect(output).to include('INFO:     from callable')
    end

    it "logs a block's return value" do
      Iodine::Logger.level = :info
      output = capture_log_output { Iodine::Logger.info { 'from block' } }
      expect(output).to include('INFO:     from block')
    end

    it 'logs arguments and the block together, block last' do
      Iodine::Logger.level = :info
      output = capture_log_output { Iodine::Logger.info('first', -> { 'second' }) { 'third' } }
      expect(output).to match(/first.*second.*third/m)
    end

    it 'does not evaluate callables or blocks when the level is silenced' do
      evaluated = []
      silenced = -> { evaluated << :callable }
      Iodine::Logger.level = :error
      output = capture_log_output do
        Iodine::Logger.debug(silenced) { evaluated << :block }
      end
      expect(output).to be_empty
      expect(evaluated).to be_empty
    end
  end

  describe '#<<' do
    it 'is an alias for info' do
      Iodine::Logger.level = :info
      output = capture_log_output { Iodine::Logger << 'via shift operator' }
      expect(output).to include('INFO:     via shift operator')
    end
  end
end
