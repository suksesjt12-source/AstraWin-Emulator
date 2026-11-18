# 20-stage roadmap

The stages are intended to run sequentially without asking for approval at every transition. A stage is marked complete only after its source and relevant tests exist; Android packaging is not proof that Windows programs or games are compatible.

| Stage | Work | Status |
|---:|---|---|
| 1 | Repository structure, CMake and Android Gradle project | Implemented; CI validation pending |
| 2 | ARM64 native runtime and Android JNI bridge | Implemented; CI validation pending |
| 3 | Guest memory foundation and bounds checks | Implemented; bounds test added |
| 4 | x86 instruction decoder and 32-bit execution subset | Prototype implemented; limited opcodes, smoke test added |
| 5 | x86-64 interpreter and instruction tests | Prototype implemented; instruction coverage very limited |
| 6 | PE32/PE32+ validation, mapping and relocations | Header inspection and bounded section mapping prototype; relocations/imports not implemented |
| 7 | Win32 API compatibility layer | Initial registry/environment for four kernel32 APIs; guest ABI bridge and broad coverage not implemented |
| 8 | Guest process and thread model | Basic process memory, thread creation/state and termination; no scheduler/context switching |
| 9 | Windows filesystem path mapping | Drive path mapper to sandbox with traversal/UNC/ADS and symlink escape checks; file I/O not implemented |
| 10 | Exceptions, events, timers and synchronization | Manual/auto-reset event and one-shot/periodic timer primitives added; exceptions and Win32 wait integration remain |
| 11 | Host graphics abstraction | Backend-neutral GraphicsDevice API and headless frame-state validation backend; no pixel renderer |
| 12 | Direct3D 9/11 compatibility planning and command translation | Not started |
| 13 | Vulkan renderer backend | Not started |
| 14 | OpenGL ES fallback renderer | Not started |
| 15 | Audio subsystem | Not started |
| 16 | Keyboard, touch and controller input | Not started |
| 17 | Android launcher, executable picker and session UI | Basic diagnostic screen implemented; picker not started |
| 18 | APK packaging, runtime permissions and asset delivery | ARM64 debug build workflow added; validation pending |
| 19 | Compatibility, stability and performance test suite | Native smoke tests added; Windows app/game tests not started |
| 20 | Final ARM64 APK release and verified compatibility report | Not started |

## Current acceptance tests

- Guest-memory reads and writes reject out-of-range requests.
- The x86-64 prototype executes a synthetic MOV/ADD/HLT sequence and verifies RAX.
- The x86 prototype executes a synthetic MOV/ADD/HLT sequence and verifies EAX.
- The PE parser recognizes a synthetic PE32+ header and the PE mapper maps a synthetic `.text` section while rejecting out-of-file raw data.
- The initial Win32 environment resolves and exercises GetLastError, SetLastError, GetTickCount64, and GetCurrentProcessId.
- Guest process tests verify image memory, main/secondary thread lifecycle, and termination.
- Guest path mapper tests verify drive mapping and reject traversal, alternate streams, and UNC paths.
- Guest sync tests verify manual/auto-reset events and one-shot/periodic timer behavior.
- Graphics tests verify device initialization, frame lifecycle, resize rules and clear-color bounds using the headless backend.
- GitHub Actions runs host-native tests before attempting the Android ARM64 debug APK build.
