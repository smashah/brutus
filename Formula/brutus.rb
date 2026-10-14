class Brutus < Formula
  desc "Find and close individual processes slowing your Mac"
  homepage "https://github.com/smashah/brutus"
  url "https://github.com/smashah/brutus/releases/download/v0.1.2/brutus-v0.1.2-macos-universal.tar.gz"
  version "0.1.2"
  sha256 "2e1c351c73721b5ab1d3af308172b6c49d679ca2fbb2c951496184968d78a54f"
  license "MIT"

  depends_on macos: :monterey

  def install
    bin.install "brutus"
  end
end
