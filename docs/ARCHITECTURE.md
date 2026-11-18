# AstraWin architecture

AstraWin is an experimental Windows executable compatibility project for Android ARM64. Android is the host platform; x86 and x86-64 are guest instruction-set targets.

## Current vertical slice

- Android Activity and JNI entry point.
- Native C++20 core built for arm64-v8a.
- Bounds-checked guest-memory buffer with a move-initialization path.
- Small x86 and x86-64 interpreter subsets for smoke testing; flags and instruction coverage are incomplete.
- PE32/PE32+ header inspection and bounded section mapping. The mapper validates SizeOfHeaders, raw/virtual ranges, overlap, and executable entry-point placement. Relocations and import resolution are not implemented.
- A small Win32 API name registry and environment for four kernel32/kernelbase calls. One environment instance is intended per guest thread so last-error state remains thread-local; guest ABI marshalling is not implemented. GetTickCount64 uses the host monotonic clock.
- A basic guest process/thread lifecycle model with an image-memory move path and a thread-count cap. There is no scheduler, CPU context switching, or process isolation.
- Windows drive-absolute path mapping into an Android sandbox with traversal, UNC, alternate-stream, relative-path, and symlink-escape checks. This is not a guest file I/O layer.
- Host synchronization primitives for manual/auto-reset events and one-shot/periodic monotonic timers; they are not yet wired to Win32 wait APIs. Timer values are capped at one year.
- Backend-neutral graphics device contract and headless frame-state backend; no Vulkan/OpenGL ES device or pixel renderer.
- Host-native CMake tests and Android ARM64 debug-APK CI workflow. CI checks APK ABI packaging; its actual run status must be checked in GitHub Actions.

## Execution model planned

1. Android UI selects and manages a guest executable.
2. The PE loader validates and maps supported PE sections into guest memory.
3. The CPU core interprets guest instructions, then later may add a tested ARM64 JIT.
4. A Win32 compatibility layer supplies the subset of operating-system services required by a target program.
5. Graphics, audio, input, files, threads, and timing are provided through explicit host interfaces.

A Windows game does not run merely because its PE header parses or its sections map. It also depends on CPU instruction coverage, Windows APIs, graphics APIs, codecs, drivers, memory behavior, and game-specific services. Those components must be completed and tested separately.

## Non-goals

The project does not embed or rename Winlator, Wine, or QEMU as its execution engine. It also does not claim complete Windows or DirectX compatibility.
