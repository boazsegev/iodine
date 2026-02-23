# frozen_string_literal: true

require 'bundler/setup'
require 'iodine'

# Add spec/support to load path so helpers can be required without full paths
$LOAD_PATH.unshift(File.join(__dir__, 'support'))

RSpec.configure do |config|
  # Ensure FIO_LOG_FATAL messages (printed before abort() via FIO_ASSERT) are
  # always visible in CI output. Level 1 = FATAL only; individual specs may
  # raise this but should not lower it below 1.
  Iodine.verbosity = 2  # floor: ERROR+FATAL visible in CI (level 1=fatal, 2=error, 3=warning, 4=info, 5=debug)

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
