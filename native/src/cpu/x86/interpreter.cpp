#include "astrawin/cpu/x86/interpreter.h"

#include <cstdint>

namespace astrawin::cpu::x86 {
namespace {

constexpr std::uint32_t kZeroFlag = 1U << 6U;

bool fetch_u8(
    const memory::GuestMemory& memory,
    std::uint32_t& eip,
    std::uint8_t& value) noexcept {
    if (!memory.read_value(eip, value)) {
        return false;
    }
    ++eip;
    return true;
}

bool fetch_u32(
    const memory::GuestMemory& memory,
    std::uint32_t& eip,
    std::uint32_t& value) noexcept {
    if (!memory.read_value(eip, value)) {
        return false;
    }
    eip += sizeof(value);
    return true;
}

void update_zero_flag(CpuState& state, std::uint32_t value) noexcept {
    if (value == 0U) {
        state.eflags |= kZeroFlag;
    } else {
        state.eflags &= ~kZeroFlag;
    }
}

}  // namespace

StepResult Interpreter::step(
    CpuState& state,
    const memory::GuestMemory& memory) const noexcept {
    if (state.halted) {
        return StepResult::kHalted;
    }

    std::uint8_t opcode = 0;
    if (!fetch_u8(memory, state.eip, opcode)) {
        return StepResult::kMemoryFault;
    }

    if (opcode == 0x90U) {
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xF4U || opcode == 0xC3U) {
        state.halted = true;
        ++state.instructions_executed;
        return StepResult::kHalted;
    }

    if (opcode >= 0xB8U && opcode <= 0xBFU) {
        std::uint32_t immediate = 0;
        if (!fetch_u32(memory, state.eip, immediate)) {
            return StepResult::kMemoryFault;
        }
        const auto register_index = static_cast<std::size_t>(opcode - 0xB8U);
        state.gpr[register_index] = immediate;
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0x05U) {
        std::uint32_t immediate = 0;
        if (!fetch_u32(memory, state.eip, immediate)) {
            return StepResult::kMemoryFault;
        }
        state.gpr[0] += immediate;
        update_zero_flag(state, state.gpr[0]);
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0x31U) {
        std::uint8_t modrm = 0;
        if (!fetch_u8(memory, state.eip, modrm)) {
            return StepResult::kMemoryFault;
        }
        if (modrm != 0xC0U) {
            return StepResult::kUnsupportedInstruction;
        }
        state.gpr[0] = 0;
        update_zero_flag(state, 0);
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xEBU) {
        std::uint8_t raw_displacement = 0;
        if (!fetch_u8(memory, state.eip, raw_displacement)) {
            return StepResult::kMemoryFault;
        }
        state.eip = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(state.eip) +
            static_cast<std::int8_t>(raw_displacement));
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xE9U) {
        std::uint32_t raw_displacement = 0;
        if (!fetch_u32(memory, state.eip, raw_displacement)) {
            return StepResult::kMemoryFault;
        }
        state.eip = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(state.eip) +
            static_cast<std::int32_t>(raw_displacement));
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    return StepResult::kUnsupportedInstruction;
}

StepResult Interpreter::run(
    CpuState& state,
    const memory::GuestMemory& memory,
    std::size_t max_steps) const noexcept {
    for (std::size_t index = 0; index < max_steps; ++index) {
        const auto result = step(state, memory);
        if (result != StepResult::kRunning) {
            return result;
        }
    }
    return StepResult::kStepLimitExceeded;
}

}  // namespace astrawin::cpu::x86
