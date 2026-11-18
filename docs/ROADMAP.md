# 20-stage roadmap

The stages are intended to run sequentially without asking for approval at every transition. A stage is marked complete only after its source and relevant tests exist; Android packaging is not proof that Windows programs or games are compatible.

| Stage | Work | Status |
|---:|---|---|
| 1 | Repository structure, CMake and Android Gradle project | Implemented; CI validation pending |
| 2 | ARM64 native runtime and Android JNI bridge | Implemented; CI validation pending |
| 3 | Guest memory foundation and bounds checks | Bounds-checked move-initialized memory with guest virtual base; tests cover out-of-range access and address overflow |
| 4 | x86 instruction decoder and 32-bit execution subset | Prototype implemented; limited opcodes, smoke test added |
| 5 | x86-64 interpreter and instruction tests | Prototype implemented; instruction coverage very limited |
| 6 | PE32/PE32+ validation, mapping and relocations | Header and section mapping with size, raw/virtual overlap, bounds and executable-entry checks; relocations/imports not implemented |
| 7 | Win32 API compatibility layer | Four kernel32/kernelbase calls, per-thread environment model, monotonic tick count; guest ABI bridge and broad coverage not implemented |
| 8 | Guest process and thread model | PE ImageBase-aware guest memory/IP, move-based image ownership, x86/x64 range checks, primary-thread start, state/termination, 1024-thread cap; no scheduler/context switching |
| 9 | Windows filesystem path mapping | Explicit drive-absolute path mapper; rejects relative/drive-relative, traversal, UNC/ADS and symlink escape; file I/O not implemented |
| 10 | Exceptions, events, timers and synchronization | Manual/auto-reset events and one-shot/periodic timers added; durations clamped; exceptions and Win32 wait integration remain |
| 11 | Host graphics abstraction | Backend-neutral GraphicsDevice API and headless frame-state validation backend; no pixel renderer |
| 12 | Direct3D 9/11 compatibility planning and command translation | Not started |
| 13 | Vulkan renderer backend | Not started |
| 14 | OpenGL ES fallback renderer | Not started |
| 15 | Audio subsystem | Not started |
| 16 | Keyboard, touch and controller input | Not started |
| 17 | Android launcher, executable picker and session UI | Basic diagnostic screen implemented; picker not started |
| 18 | APK packaging, runtime permissions and asset delivery | ARM64 debug build and ABI-content verification workflow added; successful CI build still pending verification |
| 19 | Compatibility, stability and performance test suite | Native smoke tests added; Windows app/game tests not started |
| 20 | Final ARM64 APK release and verified compatibility report | Not started |

## Current acceptance tests

- Guest-memory reads and writes reject out-of-range requests, preserve non-zero virtual bases, and reject address-range overflow.
- The x86-64 prototype executes a synthetic MOV/ADD/HLT sequence and verifies RAX.
- The x86 prototype executes a synthetic MOV/ADD/HLT sequence and verifies EAX.
- The PE parser recognizes a synthetic PE32+ header and rejects an undersized optional header.
- The PE mapper maps a synthetic .text section and rejects out-of-file raw data, overlapping raw and virtual sections, and a non-executable entry point.
- The initial Win32 environment resolves and exercises GetLastError, SetLastError, GetTickCount64, and GetCurrentProcessId; tick count is checked for monotonicity.
- Guest process tests verify image memory, main/secondary thread lifecycle, primary-thread start state, termination, and the thread-count cap.
- Guest path mapper tests verify drive mapping and reject relative/drive-relative paths, traversal, alternate streams, UNC paths, and symlink escape.
- Guest sync tests verify manual/auto-reset events and one-shot/periodic timer behavior, including very large input durations.
- Graphics tests verify device initialization, frame lifecycle, resize rules and clear-color bounds using the headless backend.
- GitHub Actions installs the pinned CMake version, runs host-native tests, builds the Android ARM64 debug APK, and verifies the APK contains only the expected ARM64 native library. CI run success has not yet been confirmed.
