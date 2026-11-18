# AstraWin architecture

AstraWin is an experimental Windows executable compatibility project for Android ARM64. Android is the host platform; x86 and x86-64 are guest instruction-set targets.

## Current vertical slice

- Android Activity and JNI entry point.
- Native C++20 core built for arm64-v8a.
- Bounds-checked guest-memory buffer with a move-initialization path and PE ImageBase-aware guest virtual addressing; address-range overflow is rejected.
- Small x86 and x86-64 interpreter subsets supporting basic register moves/adds, short/near relative branches, register/immediate PUSH, register POP, relative and indirect CALL/JMP, RET and RET imm16. The common x86 absolute-IAT CALL and x64 RIP-relative-IAT CALL forms are supported. Stack reads/writes and supported ModRM/SIB effective addresses are bounds-checked; general instruction coverage, flags, and most ModRM/SIB forms remain incomplete.
- PE32/PE32+ header inspection and bounded section mapping. The loader validates SizeOfHeaders, raw/virtual ranges, overlap, and executable entry-point placement; it applies x86 HIGHLOW and x64 DIR64 base relocations. Import descriptors and thunk/name/ordinal records are bounds-checked and enumerated. A caller-supplied callback can resolve symbols and populate the IAT; no built-in Windows DLL export resolver or broad Win32 API address map exists yet.
- A limited Win32 API registry/environment for GetLastError, SetLastError, GetTickCount64 and GetCurrentProcessId. A per-process CallDispatcher creates synthetic import thunks; supported indirect CALL instructions can reach IAT pointers, then the dispatcher routes calls through x86 stdcall stack arguments or the Windows x64 first-argument RCX convention. x86 64-bit return values use EDX:EAX. One Environment instance is intended per guest thread so last-error state remains thread-local. The dispatcher covers only those four APIs and is not yet wired into a complete Android executable session manager.
- A basic guest process/thread lifecycle model with an image-memory move path, ImageBase-aware entrypoints, architecture/range checks and a thread-count cap. There is no scheduler, CPU context switching, or process isolation.
- Windows drive-absolute path mapping into an Android sandbox with traversal, UNC, alternate-stream, relative-path, and symlink-escape checks. This is not a guest file I/O layer.
- Host synchronization primitives for manual/auto-reset events and one-shot/periodic monotonic timers; they are not yet wired to Win32 wait APIs. Timer values are capped at one year.
- Backend-neutral graphics device contract and headless frame-state backend; no Vulkan/OpenGL ES device or pixel renderer.
- Host-native CMake tests and Android ARM64 debug-APK CI workflow. CI checks APK ABI packaging; its actual run status must be checked in GitHub Actions.

## Execution model planned

1. Android UI selects a guest executable and inspects its PE header; full session management is not implemented.
2. The PE loader validates and maps supported PE sections into guest memory.
3. The CPU core interprets guest instructions, then later may add a tested ARM64 JIT.
4. The Win32 import resolver maps supported APIs to synthetic thunks; the call dispatcher marshals their guest ABI and resumes at the guest return address.
5. Graphics, audio, input, files, threads, and timing are provided through explicit host interfaces.

A Windows game does not run merely because its PE header parses or its sections map. It also depends on CPU instruction coverage, Windows APIs, graphics APIs, codecs, drivers, memory behavior, and game-specific services. Those components must be completed and tested separately.

## Non-goals

The project does not embed or rename Winlator, Wine, or QEMU as its execution engine. It also does not claim complete Windows or DirectX compatibility.
