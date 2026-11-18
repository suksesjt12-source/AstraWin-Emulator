# AstraWin Emulator

AstraWin is a from-scratch experimental Windows executable compatibility project for Android ARM64. It is not a renamed Winlator package and does not use Winlator, Wine, or QEMU as its main execution engine.

## Project status

This repository contains an Android/JNI shell, native C++20 core, bounds-checked guest memory, small x86 and x86-64 interpreter prototypes, PE32/PE32+ header inspection, and a bounded PE section mapper. The loader does not yet apply base relocations or resolve imports. The supported CPU instruction subsets are intentionally tiny and are only smoke-test foundations. AstraWin cannot yet load or run ordinary Windows applications or games. The initial Win32 API registry/environment currently implements only GetLastError, SetLastError, GetTickCount64, and GetCurrentProcessId; guest ABI marshalling and broader Win32 coverage are not implemented. A basic guest process/thread lifecycle model and sandboxed Windows path mapper are included, but the scheduler, file I/O, process isolation, DirectX translation, Vulkan/OpenGL ES renderer, audio, and full input stack are not implemented.

See the [architecture notes](docs/ARCHITECTURE.md) and [20-stage roadmap](docs/ROADMAP.md).

## Build

Requirements: JDK 17, Android SDK platform 35, Android NDK 27.0.12077973, CMake 3.22.1, and Gradle 8.9.

Build the Android ARM64 debug APK:

    gradle :app:assembleDebug

The APK is written to:

    app/build/outputs/apk/debug/app-debug.apk

Run the host-native tests:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

GitHub Actions runs the native tests first, then builds and uploads the Android debug APK as a workflow artifact.

## Important compatibility note

Parsing a Windows PE header is not equivalent to loading an executable. Running Windows software requires substantial CPU instruction coverage and compatible operating-system, graphics, audio, filesystem, and driver interfaces. Compatibility will be reported only for programs that are actually tested.
