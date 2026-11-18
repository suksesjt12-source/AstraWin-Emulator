# AstraWin architecture

AstraWin is an experimental Windows executable compatibility project for Android ARM64. Android is the host platform; x86 and x86-64 are guest instruction-set targets.

## Current vertical slice

- Android Activity and JNI entry point.
- Native C++20 core built for arm64-v8a.
- Bounds-checked guest-memory buffer.
- Small x86 and x86-64 interpreter subsets for smoke testing.
- PE32/PE32+ header inspection and bounded section mapping; relocations and import resolution are not implemented.
- A small Win32 API name registry and environment for four kernel32/kernelbase calls; no guest-call ABI bridge yet.
- A basic guest process/thread lifecycle model; no scheduler, CPU context switching, or process isolation.
- Windows drive path mapping into an Android sandbox with traversal and symlink-escape checks; no guest file I/O layer.
- Host-native CMake tests and Android debug-APK CI workflow.

## Execution model planned

1. Android UI selects and manages a guest executable.
2. The PE loader validates and maps supported PE sections into guest memory.
3. The CPU core interprets guest instructions, then later may add a tested ARM64 JIT.
4. A Win32 compatibility layer supplies the subset of operating-system services required by a target program.
5. Graphics, audio, input, files, threads, and timing are provided through explicit host interfaces.

A Windows game does not run merely because its PE header parses. It also depends on CPU instruction coverage, Windows APIs, graphics APIs, codecs, drivers, memory behavior, and game-specific services. Those components are future work and must be tested separately.

## Non-goals

The project does not embed or rename Winlator, Wine, or QEMU as its execution engine. It also does not claim complete Windows or DirectX compatibility.
