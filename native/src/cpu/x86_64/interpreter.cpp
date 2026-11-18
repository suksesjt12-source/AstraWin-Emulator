#include "astrawin/cpu/x86_64/interpreter.h"

#include <cstdint>

namespace astrawin::cpu::x86_64 {
namespace {

constexpr std::uint64_t kZeroFlag = 1ULL << 6U;

bool fetch_u8(
    const memory::GuestMemory& memory,
    std::uint64_t& rip,
    std::uint8_t& value) noexcept {
    if (!memory.read_value(rip, value)) {
        return false;
    }
    ++rip;
    return true;
}

bool fetch_u32(
    const memory::GuestMemory& memory,
    std::uint64_t& rip,
    std::uint32_t& value) noexcept {
    if (!memory.read_value(rip, value)) {
        return false;
    }
    rip += sizeof(value);
    return true;
}

bool fetch_u64(
    const memory::GuestMemory& memory,
    std::uint64_t& rip,
    std::uint64_t& value) noexcept {
    if (!memory.read_value(rip, value)) {
        return false;
    }
    rip += sizeof(value);
    return true;
}

void update_zero_flag(CpuState& state, std::uint64_t value) noexcept {
    if (value == 0) {
        state.rflags |= kZeroFlag;
    } else {
        state.rflags &= ~kZeroFlag;
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
    if (!fetch_u8(memory, state.rip, opcode)) {
        return StepResult::kMemoryFault;
    }

    std::uint8_t rex = 0;
    if (opcode >= 0x40U && opcode <= 0x4FU) {
        rex = opcode;
        if (!fetch_u8(memory, state.rip, opcode)) {
            return StepResult::kMemoryFault;
        }
    }

    if (opcode == 0x90U) {
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xF4U) {
        state.halted = true;
        ++state.instructions_executed;
        return StepResult::kHalted;
    }

    if (opcode >= 0xB8U && opcode <= 0xBFU) {
        const auto register_index = static_cast<std::size_t>(
            (opcode - 0xB8U) + ((rex & 0x1U) != 0U ? 8U : 0U));
        if ((rex & 0x8U) != 0U) {
            std::uint64_t immediate = 0;
            if (!fetch_u64(memory, state.rip, immediate)) {
                return StepResult::kMemoryFault;
            }
            state.gpr[register_index] = immediate;
        } else {
            std::uint32_t immediate = 0;
            if (!fetch_u32(memory, state.rip, immediate)) {
                return StepResult::kMemoryFault;
            }
            state.gpr[register_index] = immediate;
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0x05U) {
        std::uint32_t immediate = 0;
        if (!fetch_u32(memory, state.rip, immediate)) {
            return StepResult::kMemoryFault;
        }

        if ((rex & 0x8U) != 0U) {
            const auto signed_immediate = static_cast<std::int64_t>(
                static_cast<std::int32_t>(immediate));
            state.gpr[0] += static_cast<std::uint64_t>(signed_immediate);
            update_zero_flag(state, state.gpr[0]);
        } else {
            const auto result = static_cast<std::uint32_t>(state.gpr[0]) + immediate;
            state.gpr[0] = result;
            update_zero_flag(state, result);
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xEBU) {
        std::uint8_t displacement = 0;
        if (!fetch_u8(memory, state.rip, displacement)) {
            return StepResult::kMemoryFault;
        }
        state.rip = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(state.rip) +
            static_cast<std::int8_t>(displacement));
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xE9U) {
        std::uint32_t raw_displacement = 0;
        if (!fetch_u32(memory, state.rip, raw_displacement)) {
            return StepResult::kMemoryFault;
        }
        state.rip = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(state.rip) +
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

}  // namespace astrawin::cpu::x86_64
