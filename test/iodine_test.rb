require 'test_helper'

Iodine.warn "Iodine only tests the HPACK encode/decode automatically. Most other tests are performed manualy at this time."

class IodineTest < Minitest::Test
  def test_that_it_has_a_version_number
    refute_nil ::Iodine::VERSION
  end

  def test_something
  end

end
