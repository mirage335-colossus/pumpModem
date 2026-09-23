#!/bin/sh
# CI build/inspection tool only; application compilers and target libraries stay
# in the selected baseline. CMake 3.22 drops inherited ELF RPATHs after one
# library hop, causing false relocation conflicts. Preserve the strict verifier.
# Pins: https://github.com/Kitware/CMake/releases/download/v3.31.10/cmake-3.31.10-SHA-256.txt
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 /absolute/new/install/directory" >&2
    exit 2
fi
destination=$1
case "$destination" in
    /*) ;;
    *) echo 'CMake destination must be absolute' >&2; exit 2 ;;
esac
if [ -e "$destination" ] || [ -L "$destination" ]; then
    echo "CMake destination already exists: $destination" >&2
    exit 2
fi
case "$(uname -m)" in
    x86_64|amd64) architecture=x86_64 ;;
    aarch64|arm64) architecture=aarch64 ;;
    *) echo 'Release CMake supports Linux x86_64 and aarch64 only' >&2; exit 2 ;;
esac
[ "$(uname -s)" = Linux ] || { echo 'Release CMake requires Linux' >&2; exit 2; }

version=3.31.10
archive=cmake-$version-linux-$architecture.tar.gz
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
scratch=$(mktemp -d)
trap 'rm -rf -- "$scratch"' EXIT HUP INT TERM
curl --fail --silent --show-error --location --retry 3 --proto '=https' --tlsv1.2 \
    "https://github.com/Kitware/CMake/releases/download/v$version/$archive" \
    --output "$scratch/$archive"
(
    cd "$scratch"
    sha256sum --check --ignore-missing "$script_dir/release-cmake.sha256"
)
mkdir "$scratch/install"
tar -xzf "$scratch/$archive" --strip-components=1 -C "$scratch/install"
"$scratch/install/bin/cmake" --version
"$scratch/install/bin/ctest" --version
mkdir -p -- "$(dirname -- "$destination")"
mv -- "$scratch/install" "$destination"
echo "Installed release CMake $version; add $destination/bin to PATH"
