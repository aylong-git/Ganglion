#!/bin/bash
# Rebuild Ganglion and launch it.
# From Finder, double-click "Build Ganglion.command".
# From Terminal: ./build.sh

set -euo pipefail

cd "$(dirname "$0")"
ROOT="$(pwd)"

echo "========================================="
echo "Building Ganglion SSVEP Interface (macOS)"
echo "========================================="

if ! xcode-select -p >/dev/null 2>&1; then
    echo "[ERROR] Apple Command Line Tools are not installed."
    echo "Run this in Terminal, then run this script again:"
    echo "  xcode-select --install"
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "[1/4] CMake is missing. Installing it with Homebrew..."
    if ! command -v brew >/dev/null 2>&1; then
        echo "[ERROR] Homebrew is not installed, so CMake cannot be installed automatically."
        echo "Install Homebrew from https://brew.sh, then run this script again."
        exit 1
    fi
    brew install cmake
fi

# The checked-in brainflow-prebuilt folder is Windows DLLs.
# macOS libraries live in compiled_libs.tar.
BF_MAC="$ROOT/AppCore/external_packages/brainflow-macos"
if [ ! -f "$BF_MAC/lib/libBrainflow.a" ] || [ ! -f "$BF_MAC/lib/libBoardController.dylib" ] || [ ! -f "$BF_MAC/lib/libDataHandler.dylib" ] || [ ! -f "$BF_MAC/inc/board_shim.h" ]; then
    echo "[1/4] Preparing macOS BrainFlow libraries..."
    mkdir -p "$BF_MAC/inc" "$BF_MAC/lib"
    cp -R "$ROOT/AppCore/external_packages/brainflow-prebuilt/inc/." "$BF_MAC/inc/"

    EXTRACT="$(mktemp -d)"
    tar -xf "$ROOT/AppCore/external_packages/compiled_libs.tar" -C "$EXTRACT"
    find "$EXTRACT" -maxdepth 1 \( -name '*.dylib' -o -name 'libBrainflow.a' -o -name 'libDSPFilters.a' -o -name 'libWaveLib.a' -o -name 'libkissfft-double.a' \) -exec cp {} "$BF_MAC/lib/" \;
    rm -rf "$EXTRACT"
else
    echo "[1/4] macOS BrainFlow libraries already prepared."
fi

if [ ! -x "$ROOT/vcpkg/vcpkg" ]; then
    echo "[2/4] Setting up vcpkg package manager..."
    if [ -d "$ROOT/vcpkg" ] && [ ! -d "$ROOT/vcpkg/.git" ]; then
        if [ -z "$(ls -A "$ROOT/vcpkg")" ]; then
            rmdir "$ROOT/vcpkg"
        else
            echo "[ERROR] vcpkg/ exists but is not a vcpkg checkout."
            echo "Move that folder aside, then run this script again."
            exit 1
        fi
    fi
    if [ ! -d "$ROOT/vcpkg/.git" ]; then
        git clone https://github.com/microsoft/vcpkg.git "$ROOT/vcpkg"
    fi
    "$ROOT/vcpkg/bootstrap-vcpkg.sh"
else
    echo "[2/4] vcpkg already set up."
fi

echo "[3/4] Configuring CMake..."
cmake -B "$ROOT/build" -S "$ROOT" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/vcpkg/scripts/buildsystems/vcpkg.cmake" \
    -DBRAINFLOW_ROOT="$BF_MAC"

echo "[4/4] Compiling..."
cmake --build "$ROOT/build" --config Release -j

APP="$ROOT/build/Ganglion_SSVEP"
if [ ! -x "$APP" ]; then
    APP="$ROOT/build/Release/Ganglion_SSVEP"
fi
if [ ! -x "$APP" ]; then
    echo "[ERROR] Build finished, but Ganglion_SSVEP was not found."
    exit 1
fi

echo "========================================="
echo "Opening Ganglion_SSVEP"
echo "========================================="
exec "$APP"
