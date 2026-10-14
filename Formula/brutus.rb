class Brutus < Formula
  desc "Find and close individual processes slowing your Mac"
  homepage "https://github.com/smashah/brutus"
  url "https://github.com/smashah/brutus/releases/download/v0.1.1/brutus-v0.1.1-macos-universal.tar.gz"
  version "0.1.1"
  sha256 "438e23efd70b39d8374310d42e62f2d76e4469377183c91e765430cda42717c8"
  license "MIT"

  depends_on macos: :monterey

  def install
    bin.install "brutus"
  end
end
