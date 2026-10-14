class Brutus < Formula
  desc "Find and close individual processes slowing your Mac"
  homepage "https://github.com/smashah/brutus"
  url "https://github.com/smashah/brutus/releases/download/v0.1.0/brutus-v0.1.0-macos-universal.tar.gz"
  version "0.1.0"
  sha256 "75287769c7bf2e4179ecd82c41a681fb0612bea5f4b00e230ff0f918f86309ee"
  license "MIT"

  depends_on macos: :monterey

  def install
    bin.install "brutus"
  end
end
