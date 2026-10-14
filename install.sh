#!/bin/sh
# Download a versioned release, check its SHA-256, and install without sudo.
set -eu

version=0.1.0
repository=smashah/brutus
prefix=${BRUTUS_INSTALL_DIR:-"$HOME/.local/bin"}

if [ "$(uname -s)" != Darwin ]; then
    printf '%s\n' 'Brutus currently runs on macOS only.' >&2
    exit 1
fi
case "$(uname -m)" in
    arm64|x86_64) ;;
    *) printf '%s\n' 'This release supports Apple silicon and Intel Macs.' >&2; exit 1 ;;
esac

task_tmp=$(mktemp -d "${TMPDIR:-/tmp}/brutus-install.XXXXXX")
trap 'rm -rf "$task_tmp"' EXIT HUP INT TERM
archive="brutus-v${version}-macos-universal.tar.gz"
base="https://github.com/${repository}/releases/download/v${version}"

printf 'Downloading Brutus %s...\n' "$version"
curl --fail --location --silent --show-error --proto '=https' --tlsv1.2 \
    "$base/$archive" --output "$task_tmp/$archive"
curl --fail --location --silent --show-error --proto '=https' --tlsv1.2 \
    "$base/$archive.sha256" --output "$task_tmp/$archive.sha256"
(cd "$task_tmp" && shasum -a 256 -c "$archive.sha256")
tar -xzf "$task_tmp/$archive" -C "$task_tmp" brutus
mkdir -p "$prefix"
install -S -m 755 "$task_tmp/brutus" "$prefix/brutus"

printf '\nInstalled: %s/brutus\n' "$prefix"
case ":$PATH:" in
    *":$prefix:"*) printf '%s\n' 'Run: brutus' ;;
    *) printf 'Run: "%s/brutus"\n' "$prefix"
       printf 'To add it to your PATH, put this in your shell configuration:\n  export PATH="%s:$PATH"\n' "$prefix" ;;
esac
