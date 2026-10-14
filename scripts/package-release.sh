#!/bin/sh
# Produce distributable artifacts; this does not publish anything.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=$(cat "$project_dir/VERSION")
case "$version" in
    ''|*[!0-9.]*|.*|*.) printf '%s\n' 'VERSION must contain a numeric release version.' >&2; exit 1 ;;
esac
case "$(uname -s)" in
    Darwin) ;;
    *) printf '%s\n' 'Release packaging requires the macOS toolchain.' >&2; exit 1 ;;
esac
stage="$project_dir/build/release-$version"
dist="$project_dir/dist"
mkdir -p "$stage" "$dist" "$project_dir/Formula"

${CC:-cc} -Os -mmacosx-version-min=12.0 -arch arm64 -arch x86_64 \
    "-DBRUTUS_VERSION=\"$version\"" "$project_dir/brutus.c" -o "$stage/brutus" \
    -framework IOKit -framework CoreFoundation
strip -x "$stage/brutus"
codesign --force --sign - "$stage/brutus"
cp "$project_dir/LICENSE" "$project_dir/README.md" "$stage/"

archive="brutus-v$version-macos-universal.tar.gz"
COPYFILE_DISABLE=1 tar --uid 0 --gid 0 --uname root --gname wheel \
    -czf "$dist/$archive" -C "$stage" brutus LICENSE README.md
(cd "$dist" && shasum -a 256 "$archive" > "$archive.sha256")
archive_sha=$(cut -d ' ' -f 1 "$dist/$archive.sha256")

cat > "$project_dir/Formula/brutus.rb" <<FORMULA
class Brutus < Formula
  desc "Find and close individual processes slowing your Mac"
  homepage "https://github.com/smashah/brutus"
  url "https://github.com/smashah/brutus/releases/download/v$version/$archive"
  version "$version"
  sha256 "$archive_sha"
  license "MIT"

  depends_on macos: :monterey

  def install
    bin.install "brutus"
  end
end
FORMULA

source_archive="brutus-v$version-source.tar.gz"
COPYFILE_DISABLE=1 tar --uid 0 --gid 0 --uname root --gname wheel \
    -czf "$dist/$source_archive" -C "$project_dir" \
    brutus.c VERSION Makefile LICENSE README.md install.sh scripts docs assets Formula .gitignore
(cd "$dist" && shasum -a 256 "$source_archive" > "$source_archive.sha256")
printf '\nRelease files: %s\n' "$dist"
