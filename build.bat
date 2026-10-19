@echo off
echo =========================================
echo Building Ganglion SSVEP Interface...
echo =========================================

:: 1. Download vcpkg package manager if missing
if not exist "vcpkg\vcpkg.exe" (
    echo [1/3] Setting up vcpkg package manager...
    git clone https://github.com/microsoft/vcpkg.git
    call vcpkg\bootstrap-vcpkg.bat
)

:: 2. Configure CMake and automatically install dependencies from vcpkg.json
echo [2/3] Downloading dependencies and configuring CMake...
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake

:: 3. Build the project
echo [3/3] Compiling the project...
cmake --build build --config Release

echo =========================================
echo Build complete! Your executable is inside the 'build' or 'build/Release' folder.
pause