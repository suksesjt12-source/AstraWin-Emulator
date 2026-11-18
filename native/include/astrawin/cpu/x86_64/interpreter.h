#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "astrawin/memory/guest_memory.h"

namespace astrawin::cpu::x86_64 {

struct CpuState {
    std::array<std::uint64_t, 16> gpr{};
    std::uint64_t rip{0};
    std::uint64_t rflags{0x2};
    std::uint64_t instructions_executed{0};
    bool halted{false};
};

enum class StepResult {
    kRunning,
    kHalted,
    kUnsupportedInstruction,
    kMemoryFault,
    kStepLimitExceeded
};

class Interpreter {
public:
    [[nodiscard]] StepResult step(
        CpuState& state,
        memory::GuestMemory& memory) const noexcept;

    [[nodiscard]] StepResult run(
        CpuState& state,
        memory::GuestMemory& memory,
        std::size_t max_steps) const noexcept;
};

}  // namespace astrawin::cpu::x86_64
