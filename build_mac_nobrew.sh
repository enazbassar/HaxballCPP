#!/bin/bash
set -e

echo "=== Haxball C++ Client - macOS No-Brew Standalone Build ==="

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
MAC_DEPS="$BASE_DIR/mac_deps"
mkdir -p "$MAC_DEPS"

# 1. Download SDL2 official macOS framework (zero Homebrew required)
if [ ! -d "$MAC_DEPS/SDL2.framework" ]; then
    echo "--> Downloading official SDL2.framework for macOS..."
    curl -sL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.dmg -o "$MAC_DEPS/SDL2.dmg"
    hdiutil attach "$MAC_DEPS/SDL2.dmg" -mountpoint "$MAC_DEPS/sdl2_mount" -quiet -nobrowse
    cp -R "$MAC_DEPS/sdl2_mount/SDL2.framework" "$MAC_DEPS/"
    hdiutil detach "$MAC_DEPS/sdl2_mount" -quiet
    rm -f "$MAC_DEPS/SDL2.dmg"
fi

echo "--> SDL2 framework ready."
echo "--> Note: If you have Xcode or Apple Command Line Tools installed (clang), you can compile with:"
echo "    xcode-select --install"
