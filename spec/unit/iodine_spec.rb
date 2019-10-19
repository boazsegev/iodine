RSpec.describe Iodine do
  describe '.running?' do
    it 'is false when Iodine is not running' do
      expect(Iodine.running?).to be(false)
    end
  end
end
