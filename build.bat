@echo off
echo =========================================
echo Building Ganglion SSVEP Interface...
echo =========================================

:: 0. Safety Check: Lock in the MSVC Compiler (Blocks MinGW/Strawberry Perl)
set "VSCMD_START_DIR=%CD%"
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)
if defined VS_PATH (
    echo [0/3] Initializing Visual Studio C++ Compiler...
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
) else (
    echo [ERROR] Visual Studio C++ Compiler not found!
    echo Please ensure you have "Desktop development with C++" installed via Visual Studio Installer.
    pause
    exit /b 1
)

:: 1. Download vcpkg package manager if missing
if not exist "vcpkg\vcpkg.exe" (
    echo [1/3] Setting up vcpkg package manager...
    git clone https://github.com/microsoft/vcpkg.git
    call vcpkg\bootstrap-vcpkg.bat
)

:: 2. Configure CMake explicitly using MSVC
echo [2/3] Downloading dependencies and configuring CMake...
cmake -B build -S . ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%cd%\vcpkg\scripts\buildsystems\vcpkg.cmake" ^
  -DBRAINFLOW_ROOT="%cd%\AppCore\external_packages\brainflow-prebuilt"

if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

:: 3. Build the project
echo [3/3] Compiling the project...
cmake --build build --config Release

echo =========================================
echo Build complete! Your executable is inside the 'build' or 'build/Release' folder.
pause