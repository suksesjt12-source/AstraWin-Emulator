#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "astrawin/memory/guest_memory.h"

namespace astrawin::cpu::x86 {

struct CpuState {
    std::array<std::uint32_t, 8> gpr{};
    std::uint32_t eip{0};
    std::uint32_t eflags{0x2U};
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
        const memory::GuestMemory& memory) const noexcept;

    [[nodiscard]] StepResult run(
        CpuState& state,
        const memory::GuestMemory& memory,
        std::size_t max_steps) const noexcept;
};

}  // namespace astrawin::cpu::x86
