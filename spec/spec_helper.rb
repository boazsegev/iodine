# frozen_string_literal: true

require 'bundler/setup'
require 'iodine'

# Flush stdio immediately so output survives a hard process termination in CI.
$stdout.sync = true
$stderr.sync = true

# CI diagnostic (Windows silent exit-1 hunt): proves whether the process
# reaches normal Ruby teardown and reveals any in-flight exit cause.
# - Line ABSENT from CI log  => hard termination (native crash, exit!,
#   or TerminateProcess) - investigate the C layer with a crash dump.
# - Line PRESENT w/ SystemExit => something called exit; backtrace names
#   the exact call site (exit from ANY thread kills the process silently).
# - Line PRESENT, cause none   => clean VM teardown; RSpec's own output
#   or exit path is what broke.
at_exit do
  cause = $!
  warn "[spec_helper] at_exit reached - cause: " \
       "#{cause ? "#{cause.class}: #{cause.message}" : 'none (clean exit)'}"
  warn(cause.backtrace.first(10).join("\n")) if cause&.backtrace
end

# Add spec/support to load path so helpers can be required without full paths
$LOAD_PATH.unshift(File.join(__dir__, 'support'))

# Test verbosity level (Iodine::Logger levels: NONE=0, FATAL=1, ERROR=2,
# WARN=3, INFO=4, DEBUG=5). Override with IODINE_LOG_LEVEL for CI diagnostics.
TEST_VERBOSITY = ENV['IODINE_LOG_LEVEL'] ? ENV['IODINE_LOG_LEVEL'].to_i : Iodine::Logger::WARN

RSpec.configure do |config|
  # Ensure FIO_LOG_FATAL messages (printed before abort() via FIO_ASSERT) are
  # always visible in CI output. Level 1 = FATAL only; individual specs may
  # raise this but should not lower it below 1.
  Iodine::Logger.level = TEST_VERBOSITY  # DEBUG: all C extension messages visible for CI diagnostics

  config.expect_with :rspec do |expectations|
    expectations.include_chain_clauses_in_custom_matcher_descriptions = true
  end

  config.mock_with :rspec do |mocks|
    mocks.verify_partial_doubles = true
  end

  config.shared_context_metadata_behavior = :apply_to_host_groups
  config.filter_run_when_matching :focus
  config.example_status_persistence_file_path = 'spec/examples.txt'
  config.disable_monkey_patching!
  config.warnings = true

  config.default_formatter = 'doc' if config.files_to_run.one?

  # Use defined order to ensure reactor-lifecycle specs run in a predictable
  # sequence. Iodine.on_state(:start) callbacks accumulate across reactor
  # restarts; random ordering can cause cross-spec interference.
  config.order = :defined

  # Keep failure output clean: filter gem/internal frames out of backtraces
  # so only application and spec lines are shown.
  config.filter_run_excluding :skip
  config.backtrace_exclusion_patterns = [
    /\/gems\//,
    /\/rubygems\//,
    /\/bin\//,
    /spec_helper\.rb/,
    /RSpec/,
  ]

  # On CI, progress format only shows an 'F' — print full failure details
  # immediately when each example fails so remote logs are self-contained.
  # Backtrace is only shown for unexpected exceptions (not assertion failures).
  config.after(:each) do |example|
    next unless example.exception

    e = example.exception
    puts "\n[FAILED] #{example.full_description}"
    puts "  Location : #{example.location}"
    puts "  Error    : #{e.class}: #{e.message}"
    unless e.is_a?(RSpec::Expectations::ExpectationNotMetError)
      puts "  Backtrace:\n#{e.backtrace.map { |l| "    #{l}" }.join("\n")}"
    end
    puts
  end
end
