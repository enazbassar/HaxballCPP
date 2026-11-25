#!/bin/bash
set -e

echo "=== Haxball C++ Client - macOS Build ==="

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
MAC_DEPS="$BASE_DIR/mac_deps"
mkdir -p "$MAC_DEPS"

# 1. Download SDL2 official macOS framework if missing
if [ ! -d "$MAC_DEPS/SDL2.framework" ]; then
    echo "--> Downloading official SDL2.framework for macOS..."
    curl -sL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.dmg -o "$MAC_DEPS/SDL2.dmg"
    hdiutil attach "$MAC_DEPS/SDL2.dmg" -mountpoint "$MAC_DEPS/sdl2_mount" -quiet -nobrowse
    cp -R "$MAC_DEPS/sdl2_mount/SDL2.framework" "$MAC_DEPS/"
    hdiutil detach "$MAC_DEPS/sdl2_mount" -quiet
    rm -f "$MAC_DEPS/SDL2.dmg"
fi

# 2. Build libdatachannel if missing
if [ ! -f "$MAC_DEPS/libdatachannel.dylib" ]; then
    echo "--> Building libdatachannel for macOS..."
    mkdir -p "$MAC_DEPS/build_datachannel"
    cd "$MAC_DEPS/build_datachannel"
    if [ ! -f "CMakeLists.txt" ]; then
        git clone --depth 1 --recursive https://github.com/paullouisageneau/libdatachannel.git .
    fi
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DNO_MEDIA=ON -DNO_WEBSOCKET=OFF -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl@3
    make -j$(sysctl -n hw.ncpu)
    cp -P libdatachannel*.dylib "$MAC_DEPS/"
    cd "$BASE_DIR"
fi

echo "--> Configuring build..."
mkdir -p "$BASE_DIR/build-mac"
cd "$BASE_DIR/build-mac"

cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENSSL_ROOT_DIR="/usr/local/opt/openssl@3"

echo "--> Compiling Haxball C++ Client..."
make -j$(sysctl -n hw.ncpu)

echo ""
echo "=== Done! ==="
echo "Launch the client with: ./build-mac/haxball_client"
