# AstraWin Emulator

AstraWin is a from-scratch experimental Windows executable compatibility project for Android ARM64. It is not a renamed Winlator package and does not use Winlator, Wine, or QEMU as its main execution engine.

## Project status

This repository contains an Android/JNI shell, a native C++20 core, bounds-checked guest memory, small x86 and x86-64 interpreter prototypes, PE32/PE32+ header inspection, and a bounded PE section mapper. The PE mapper validates header size, rejects overlapping raw/virtual section ranges, and requires a non-zero entry point to fall inside an executable section. It does not apply base relocations or resolve imports.

The CPU interpreters support only tiny instruction subsets and are smoke-test foundations, not general x86 emulators. AstraWin cannot yet load and run ordinary Windows applications or games. The initial Win32 API registry/environment implements only GetLastError, SetLastError, GetTickCount64, and GetCurrentProcessId. The environment is intended to be instantiated once per guest thread; guest ABI marshalling and broader Win32 coverage are not implemented.

A basic guest process/thread lifecycle model is present. PE image storage can be moved into guest memory to avoid an extra full-image copy; guest virtual reads/writes preserve the PE ImageBase, and 32-bit/64-bit address-range overflow is rejected. Guest thread creation is capped. A sandboxed Windows path mapper accepts explicit drive-absolute paths such as C:\Games\app.exe and rejects relative/drive-relative paths, traversal, UNC paths, alternate data streams, and symlink escapes. It is not a file I/O implementation.

Manual/auto-reset event and one-shot/periodic timer primitives are included. Timer durations are clamped to one year to avoid monotonic-clock overflow. A backend-neutral graphics device contract and a headless validation backend are included; there is no on-screen Vulkan or OpenGL ES renderer yet.

See the architecture notes (docs/ARCHITECTURE.md) and 20-stage roadmap (docs/ROADMAP.md).

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

GitHub Actions runs native tests first, builds the Android debug APK, checks that the APK contains the ARM64 native library and no other native ABI, then uploads the APK as a workflow artifact. A successful build still does not prove Windows application compatibility.

## Important compatibility note

Parsing or mapping a Windows PE image is not equivalent to executing it. Running Windows software requires broad CPU instruction coverage and compatible operating-system, graphics, audio, filesystem, threading, and driver interfaces. Compatibility will be reported only for programs that are actually tested.
