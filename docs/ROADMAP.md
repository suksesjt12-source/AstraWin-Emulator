# 20-stage roadmap

The stages are intended to run sequentially without asking for approval at every transition. A stage is marked complete only after its source and relevant tests exist; Android packaging is not proof that Windows programs or games are compatible.

| Stage | Work | Status |
|---:|---|---|
| 1 | Repository structure, CMake and Android Gradle project | Implemented; CI validation pending |
| 2 | ARM64 native runtime and Android JNI bridge | Implemented; CI validation pending |
| 3 | Guest memory foundation and bounds checks | Implemented; host test added |
| 4 | x86 instruction decoder and 32-bit execution subset | Not started |
| 5 | x86-64 interpreter and instruction tests | Prototype implemented; instruction coverage very limited |
| 6 | PE32/PE32+ validation, mapping and relocations | Header inspection prototype only |
| 7 | Win32 API compatibility layer | Not started |
| 8 | Guest process and thread model | Not started |
| 9 | Windows filesystem path mapping | Not started |
| 10 | Exceptions, events, timers and synchronization | Not started |
| 11 | Host graphics abstraction | Not started |
| 12 | Direct3D 9/11 compatibility planning and command translation | Not started |
| 13 | Vulkan renderer backend | Not started |
| 14 | OpenGL ES fallback renderer | Not started |
| 15 | Audio subsystem | Not started |
| 16 | Keyboard, touch and controller input | Not started |
| 17 | Android launcher, executable picker and session UI | Basic diagnostic screen implemented; picker not started |
| 18 | APK packaging, runtime permissions and asset delivery | Debug build workflow added; validation pending |
| 19 | Compatibility, stability and performance test suite | Native smoke tests added; end-to-end tests not started |
| 20 | Final ARM64 APK release and verified compatibility report | Not started |

## Current acceptance tests

- Guest-memory reads and writes reject out-of-range requests.
- The x86-64 prototype executes a tiny synthetic MOV/ADD/HLT sequence and verifies RAX.
- The PE parser recognizes a synthetic PE32+ header and rejects malformed input through explicit errors.
- GitHub Actions runs host-native tests before attempting the Android ARM64 debug APK build.
