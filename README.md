# Ganglion SSVEP

Desktop app for the OpenBCI Ganglion board. Windows and macOS each build their own copy. The vcpkg checkout and the compiled dependencies are created on the machine that builds the app. They are not stored in git.

## What belongs in git

| Path | In git | Why |
| --- | --- | --- |
| `vcpkg.json` | Yes | Names the libraries every machine must install (GLFW, GLAD, Eigen). |
| `CMakeLists.txt` | Yes | Build description. |
| `build.bat` | Yes | Windows build. Downloads vcpkg if it is missing. |
| `build.sh`, `Build Ganglion.command` | Yes | macOS build. Downloads vcpkg if it is missing, then opens the app. |
| `AppCore/` | Yes | Source, assets, and the BrainFlow binaries the app links. |
| `vcpkg/` | No | A local clone of vcpkg, bootstrapped for this operating system. |
| `vcpkg_installed/` | No | Libraries vcpkg compiled for this computer. |
| `build/` | No | CMake output and the `Ganglion_SSVEP` executable. |

`vcpkg.json` has a `builtin-baseline` commit. Two machines can each install their own `vcpkg/` folder and still receive the same library versions.

## macOS

1. Install Apple’s command line tools if Terminal asks for them:

   ```bash
   xcode-select --install
   ```

2. Install CMake. Homebrew is enough:

   ```bash
   brew install cmake
   ```

3. In Finder, open this folder and double-click **Build Ganglion.command**.

   The first run clones vcpkg into `vcpkg/`, compiles the dependencies, builds the app, and opens it. Later runs only recompile what changed.

From Terminal, the same script is:

```bash
./build.sh
```

The app is `build/Ganglion_SSVEP`.

## Windows

1. Install Visual Studio with the **Desktop development with C++** workload.
2. Install CMake and Git, and make sure both are on `PATH`.
3. Double-click `build.bat`.

The first run clones vcpkg into `vcpkg\`, compiles the dependencies, and builds the app. The executable is in `build\` or `build\Release\`.

BrainFlow on Windows comes from `AppCore/external_packages/brainflow-prebuilt`. On macOS, `build.sh` unpacks the Mac libraries from `AppCore/external_packages/compiled_libs.tar` into `AppCore/external_packages/brainflow-macos/`.
