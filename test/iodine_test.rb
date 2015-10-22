require 'test_helper'

Iodine.protocol = nil

class IodineTest < Minitest::Test
  def test_that_it_has_a_version_number
    refute_nil ::Iodine::VERSION
  end

  # def test_it_does_something_useful
  #   assert false
  # end
end
