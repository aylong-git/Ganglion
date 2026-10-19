#!/bin/bash
echo "========================================="
echo "Building Ganglion SSVEP Interface (macOS)"
echo "========================================="

# 1. Download vcpkg package manager if missing
if [ ! -f "vcpkg/vcpkg" ]; then
    echo "[1/3] Setting up vcpkg package manager..."
    git clone https://github.com/microsoft/vcpkg.git
    ./vcpkg/bootstrap-vcpkg.sh
fi

# 2. Configure CMake
echo "[2/3] Downloading dependencies and configuring CMake..."
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
if [ $? -ne 0 ]; then
    echo "[ERROR] CMake configuration failed."
    echo "Please ensure you have Apple Command Line Tools installed by running: xcode-select --install"
    exit 1
fi

# 3. Build the project
echo "[3/3] Compiling the project..."
cmake --build build --config Release

echo "========================================="
echo "Build complete! Your executable is inside the 'build' folder."