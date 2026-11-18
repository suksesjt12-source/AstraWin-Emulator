# 20-stage roadmap

The stages are intended to run sequentially without asking for approval at every transition. A stage is marked complete only after its source and relevant tests exist; Android packaging is not proof that Windows programs or games are compatible.

| Stage | Work | Status |
|---:|---|---|
| 1 | Repository structure, CMake and Android Gradle project | Implemented; CI validation pending |
| 2 | ARM64 native runtime and Android JNI bridge | Implemented; CI validation pending |
| 3 | Guest memory foundation and bounds checks | Bounds-checked move-initialized memory with guest virtual base; tests cover out-of-range access and address overflow |
| 4 | x86 instruction decoder and 32-bit execution subset | Limited MOV/ADD/branch, PUSH/POP register/immediate, relative and indirect CALL/JMP, RET/RET imm16; x86 absolute-IAT CALL and stack bounds are tested. Most x86 opcodes remain unsupported |
| 5 | x86-64 interpreter and instruction tests | Limited MOV/ADD/branch, PUSH/POP including R8-R15, relative and indirect CALL/JMP, RET/RET imm16; x64 RIP-relative-IAT CALL and checked stack access are tested. General ModRM/SIB and most x64 opcodes remain unsupported |
| 6 | PE32/PE32+ validation, mapping and relocations | Header/section validation; x86 HIGHLOW and x64 DIR64 base relocation; bounded import descriptor/thunk parsing and callback-driven IAT resolution. Built-in Windows DLL resolver is not implemented |
| 7 | Win32 API compatibility layer | Four kernel32/kernelbase calls with synthetic import thunks; x86 stdcall stack args/cleanup and x64 RCX argument + return handling are tested. No general DLL export resolver or broad API coverage |
| 8 | Guest process and thread model | PE ImageBase-aware guest memory/IP, move-based image ownership, architecture and x86/x64 range checks, primary-thread start, state/termination, 1024-thread cap; no scheduler/context switching |
| 9 | Windows filesystem path mapping | Explicit drive-absolute path mapper; rejects relative/drive-relative, traversal, UNC/ADS and symlink escape; file I/O not implemented |
| 10 | Exceptions, events, timers and synchronization | Manual/auto-reset events and one-shot/periodic timers added; durations clamped; exceptions and Win32 wait integration remain |
| 11 | Host graphics abstraction | Backend-neutral GraphicsDevice API and headless frame-state validation backend; no pixel renderer |
| 12 | Direct3D 9/11 compatibility planning and command translation | Not started |
| 13 | Vulkan renderer backend | Not started |
| 14 | OpenGL ES fallback renderer | Not started |
| 15 | Audio subsystem | Not started |
| 16 | Keyboard, touch and controller input | Not started |
| 17 | Android launcher, executable picker and session UI | Diagnostic screen and document picker implemented; selected EXE headers are inspected via JNI. Guest execution/session UI not implemented |
| 18 | APK packaging, runtime permissions and asset delivery | ARM64 debug build and ABI-content verification workflow added; successful CI build still pending verification |
| 19 | Compatibility, stability and performance test suite | Native smoke tests added; Windows app/game tests not started |
| 20 | Final ARM64 APK release and verified compatibility report | Not started |

## Current acceptance tests

- Guest-memory reads and writes reject out-of-range requests, preserve non-zero virtual bases, and reject address-range overflow.
- The x86-64 prototype executes a synthetic MOV/ADD/HLT sequence and verifies RAX.
- The x86 prototype executes a synthetic MOV/ADD/HLT sequence and verifies EAX.
- x86/x64 CPU tests verify register/immediate stack operations, relative and indirect CALL, RET/RET imm16 stack cleanup, x64 R8/R9 access, and stack-boundary faults. Dispatcher integration tests execute x86 absolute-IAT and x64 RIP-relative-IAT API calls through thunk return.
- The PE parser recognizes synthetic PE32+ and PE32 headers, rejects an undersized optional header, and rejects mismatched machine/optional-header formats.
- The PE mapper maps a synthetic .text section and rejects out-of-file raw data, overlapping raw and virtual sections, and a non-executable entry point.
- PE32 HIGHLOW and PE32+ DIR64 relocation tests verify image-base delta application; malformed relocation blocks and rebasing without a relocation directory are rejected.
- Import tests verify bounded module/name thunk parsing, unresolved-import reporting without a resolver, callback-driven symbol resolution and IAT patching.
- The initial Win32 environment resolves and exercises GetLastError, SetLastError, GetTickCount64, and GetCurrentProcessId; tick count is checked for monotonicity.
- Win32 call-dispatcher tests verify supported API thunk registration, x86 stdcall argument marshalling/stack cleanup, x86 GetTickCount64 EDX:EAX return, x64 SetLastError RCX marshalling, x64 return address handling, and rejection of unsupported DLL imports.
- Guest process tests verify ImageBase-aware image memory and entrypoints, main/secondary thread lifecycle, primary-thread start state, termination, the thread-count cap, invalid architecture metadata, and x86/x64 address-range overflow.
- Guest path mapper tests verify drive mapping and reject relative/drive-relative paths, traversal, alternate streams, UNC paths, and symlink escape.
- Guest sync tests verify manual/auto-reset events and one-shot/periodic timer behavior, including very large input durations.
- Graphics tests verify device initialization, frame lifecycle, resize rules and clear-color bounds using the headless backend.
- GitHub Actions installs pinned Android SDK/NDK/CMake versions, runs native tests with ASan/UBSan, builds the Android ARM64 debug APK, and verifies the APK contains only the expected ARM64 native library. The most recent CI result must be checked against the commit being evaluated.
