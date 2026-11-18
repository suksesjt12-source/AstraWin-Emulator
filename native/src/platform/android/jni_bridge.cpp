#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

#include "astrawin/cpu/x86_64/interpreter.h"
#include "astrawin/memory/guest_memory.h"

namespace {

std::string engine_info() {
    return "AstraWin native core 0.1.0\n"
           "Android ABI: arm64-v8a\n"
           "Guest architecture: x86-64 (experimental interpreter)\n"
           "Windows API / DirectX compatibility: not implemented yet";
}

std::string run_self_test() {
    using astrawin::cpu::x86_64::CpuState;
    using astrawin::cpu::x86_64::Interpreter;
    using astrawin::cpu::x86_64::StepResult;
    using astrawin::memory::GuestMemory;

    GuestMemory memory(128);
    const std::vector<std::uint8_t> program{
        0x48U, 0xB8U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x48U, 0x05U, 0x05U, 0x00U, 0x00U, 0x00U,
        0xF4U
    };

    if (!memory.write(0, program)) {
        return "FAIL: unable to load synthetic x86-64 test program";
    }

    CpuState state{};
    Interpreter interpreter;
    const auto result = interpreter.run(state, memory, 16);
    if (result == StepResult::kHalted && state.gpr[0] == 12U &&
        state.instructions_executed == 3U) {
        return "PASS: MOV RAX,7; ADD RAX,5; HLT\nRAX = 12\nExecuted instructions = 3";
    }

    return "FAIL: interpreter self-test did not produce the expected result";
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
