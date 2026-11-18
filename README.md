# AstraWin Emulator

AstraWin is a from-scratch experimental Windows executable compatibility project for Android ARM64. It is not a renamed Winlator package and does not use Winlator, Wine, or QEMU as its main execution engine.

## Project status

This repository contains an Android/JNI shell, a native C++20 core, bounds-checked guest memory, small x86 and x86-64 interpreter prototypes, PE32/PE32+ header inspection, and a bounded PE image mapper. The mapper validates header size, rejects overlapping raw/virtual section ranges, requires a non-zero entry point to fall inside an executable section, and applies x86 HIGHLOW / x64 DIR64 base relocations when loading away from the preferred ImageBase. Import descriptors and thunk tables are validated and enumerated; an optional caller-supplied callback can resolve symbols and patch the IAT. There is not yet a built-in Windows DLL/export resolver.

The CPU interpreters support only small instruction subsets. They now include bounded stack operations, relative CALL, RET and RET imm16 for x86/x64, but remain prototypes rather than general x86 emulators. AstraWin cannot yet load and run ordinary Windows applications or games. The Win32 API registry/environment covers only GetLastError, SetLastError, GetTickCount64, and GetCurrentProcessId. A thunk dispatcher can route those imports using x86 stdcall stack arguments and the Windows x64 register convention; unsupported DLL exports and broader Win32 coverage remain unavailable. The environment is intended to be instantiated once per guest thread.

The Android launcher has a document picker that can inspect the selected executable's PE headers; it does not execute the file. A basic guest process/thread lifecycle model is present. PE image storage can be moved into guest memory to avoid an extra full-image copy; guest virtual reads/writes preserve the PE ImageBase, and invalid architecture metadata or 32-bit/64-bit address-range overflow is rejected. Guest thread creation is capped. A sandboxed Windows path mapper accepts explicit drive-absolute paths such as C:\Games\app.exe and rejects relative/drive-relative paths, traversal, UNC paths, alternate data streams, and symlink escapes. It is not a file I/O implementation.

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

GitHub Actions runs native tests with AddressSanitizer and UndefinedBehaviorSanitizer, builds the Android debug APK, checks that the APK contains the ARM64 native library and no other native ABI, then uploads the APK as a workflow artifact. The workflow uses maintained action releases, but its run result still needs verification in GitHub Actions. A successful build does not prove Windows application compatibility.

## Important compatibility note

Parsing or mapping a Windows PE image is not equivalent to executing it. Running Windows software requires broad CPU instruction coverage and compatible operating-system, graphics, audio, filesystem, threading, and driver interfaces. Compatibility will be reported only for programs that are actually tested.
