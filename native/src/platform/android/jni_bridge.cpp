#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "astrawin/cpu/x86/interpreter.h"
#include "astrawin/cpu/x86_64/interpreter.h"
#include "astrawin/memory/guest_memory.h"

namespace {

std::string engine_info() {
    return "AstraWin native core 0.1.0\n"
           "Android ABI: arm64-v8a\n"
           "Guest architecture: x86 and x86-64 (experimental subsets)\n"
           "Windows API / DirectX compatibility: not implemented yet";
}

std::string run_self_test() {
    using astrawin::memory::GuestMemory;

    GuestMemory memory(128);
    const std::vector<std::uint8_t> x64_program{
        0x48U, 0xB8U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x48U, 0x05U, 0x05U, 0x00U, 0x00U, 0x00U,
        0xF4U
    };
    const std::vector<std::uint8_t> x86_program{
        0xB8U, 0x06U, 0x00U, 0x00U, 0x00U,
        0x05U, 0x01U, 0x00U, 0x00U, 0x00U,
        0xF4U
    };

    if (!memory.write(0, x64_program) || !memory.write(32, x86_program)) {
        return "FAIL: unable to load synthetic CPU test programs";
    }

    astrawin::cpu::x86_64::CpuState state64{};
    astrawin::cpu::x86_64::Interpreter interpreter64;
    const auto result64 = interpreter64.run(state64, memory, 16);

    astrawin::cpu::x86::CpuState state32{};
    state32.eip = 32;
    astrawin::cpu::x86::Interpreter interpreter32;
    const auto result32 = interpreter32.run(state32, memory, 16);

    const bool x64_ok =
        result64 == astrawin::cpu::x86_64::StepResult::kHalted &&
        state64.gpr[0] == 12U && state64.instructions_executed == 3U;
    const bool x86_ok =
        result32 == astrawin::cpu::x86::StepResult::kHalted &&
        state32.gpr[0] == 7U && state32.instructions_executed == 3U;

    if (x86_ok && x64_ok) {
        return "PASS: x86 and x86-64 interpreter smoke tests\n"
               "x86 EAX = 7; x86-64 RAX = 12";
    }
    return "FAIL: CPU interpreter smoke test did not match expected registers";
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_io_astrawin_launcher_MainActivity_nativeGetEngineInfo(
    JNIEnv* env, jobject /* unused */) {
    const auto info = engine_info();
    return env->NewStringUTF(info.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_astrawin_launcher_MainActivity_nativeRunSelfTest(
    JNIEnv* env, jobject /* unused */) {
    const auto result = run_self_test();
    return env->NewStringUTF(result.c_str());
}
