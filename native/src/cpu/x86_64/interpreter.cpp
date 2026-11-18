#include "astrawin/cpu/x86_64/interpreter.h"

#include <cstdint>
#include <limits>

namespace astrawin::cpu::x86_64 {
namespace {

constexpr std::uint64_t kZeroFlag = 1ULL << 6U;
constexpr std::size_t kStackRegister = 4U;

bool fetch_u8(
    memory::GuestMemory& memory,
    std::uint64_t& rip,
    std::uint8_t& value) noexcept {
    if (!memory.read_value(rip, value)) {
        return false;
    }
    ++rip;
    return true;
}

bool fetch_u16(
    memory::GuestMemory& memory,
    std::uint64_t& rip,
    std::uint16_t& value) noexcept {
    if (!memory.read_value(rip, value)) {
        return false;
    }
    rip += sizeof(value);
    return true;
}

bool fetch_u32(
    memory::GuestMemory& memory,
    std::uint64_t& rip,
    std::uint32_t& value) noexcept {
    if (!memory.read_value(rip, value)) {
        return false;
    }
    rip += sizeof(value);
    return true;
}

bool fetch_u64(
    memory::GuestMemory& memory,
    std::uint64_t& rip,
    std::uint64_t& value) noexcept {
    if (!memory.read_value(rip, value)) {
        return false;
    }
    rip += sizeof(value);
    return true;
}

bool push_u64(
    CpuState& state,
    memory::GuestMemory& memory,
    std::uint64_t value) noexcept {
    const auto stack_pointer = state.gpr[kStackRegister];
    if (stack_pointer < sizeof(value)) {
        return false;
    }
    const auto next_stack_pointer = stack_pointer - sizeof(value);
    if (!memory.write_value(next_stack_pointer, value)) {
        return false;
    }
    state.gpr[kStackRegister] = next_stack_pointer;
    return true;
}

bool pop_u64(
    CpuState& state,
    memory::GuestMemory& memory,
    std::uint64_t& value,
    std::uint16_t stack_adjustment = 0U) noexcept {
    const auto stack_pointer = state.gpr[kStackRegister];
    const auto increment = static_cast<std::uint64_t>(sizeof(value)) +
                           stack_adjustment;
    if (increment > std::numeric_limits<std::uint64_t>::max() - stack_pointer) {
        return false;
    }
    if (!memory.read_value(stack_pointer, value)) {
        return false;
    }
    state.gpr[kStackRegister] += increment;
    return true;
}

bool relative_target(
    std::uint64_t base,
    std::int64_t displacement,
    std::uint64_t& target) noexcept {
    if (displacement >= 0) {
        const auto amount = static_cast<std::uint64_t>(displacement);
        if (amount > std::numeric_limits<std::uint64_t>::max() - base) {
            return false;
        }
        target = base + amount;
        return true;
    }

    const auto amount = static_cast<std::uint64_t>(-displacement);
    if (amount > base) {
        return false;
    }
    target = base - amount;
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
    memory::GuestMemory& memory) const noexcept {
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

    if (opcode >= 0x50U && opcode <= 0x57U) { // PUSH r64
        const auto register_index = static_cast<std::size_t>(
            (opcode - 0x50U) + ((rex & 0x1U) != 0U ? 8U : 0U));
        const auto value = state.gpr[register_index];
        if (!push_u64(state, memory, value)) {
            return StepResult::kMemoryFault;
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode >= 0x58U && opcode <= 0x5FU) { // POP r64
        const auto register_index = static_cast<std::size_t>(
            (opcode - 0x58U) + ((rex & 0x1U) != 0U ? 8U : 0U));
        std::uint64_t value = 0;
        if (!pop_u64(state, memory, value)) {
            return StepResult::kMemoryFault;
        }
        // POP RSP replaces the incremented stack pointer with the popped value.
        state.gpr[register_index] = value;
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0x68U) { // PUSH imm32, sign-extended to 64 bits
        std::uint32_t raw_immediate = 0;
        if (!fetch_u32(memory, state.rip, raw_immediate)) {
            return StepResult::kMemoryFault;
        }
        const auto immediate = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(static_cast<std::int32_t>(raw_immediate)));
        if (!push_u64(state, memory, immediate)) {
            return StepResult::kMemoryFault;
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0x6AU) { // PUSH imm8, sign-extended to 64 bits
        std::uint8_t raw_immediate = 0;
        if (!fetch_u8(memory, state.rip, raw_immediate)) {
            return StepResult::kMemoryFault;
        }
        const auto immediate = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(static_cast<std::int8_t>(raw_immediate)));
        if (!push_u64(state, memory, immediate)) {
            return StepResult::kMemoryFault;
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xC3U || opcode == 0xC2U) { // RET / RET imm16
        std::uint16_t stack_adjustment = 0U;
        if (opcode == 0xC2U &&
            !fetch_u16(memory, state.rip, stack_adjustment)) {
            return StepResult::kMemoryFault;
        }
        std::uint64_t return_address = 0;
        if (!pop_u64(state, memory, return_address, stack_adjustment)) {
            return StepResult::kMemoryFault;
        }
        state.rip = return_address;
        ++state.instructions_executed;
        return StepResult::kRunning;
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
        std::uint64_t target = 0;
        if (!relative_target(
                state.rip, static_cast<std::int8_t>(displacement), target)) {
            return StepResult::kMemoryFault;
        }
        state.rip = target;
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xE9U || opcode == 0xE8U) { // JMP rel32 / CALL rel32
        std::uint32_t raw_displacement = 0;
        if (!fetch_u32(memory, state.rip, raw_displacement)) {
            return StepResult::kMemoryFault;
        }
        const auto return_address = state.rip;
        std::uint64_t target = 0;
        if (!relative_target(
                return_address, static_cast<std::int32_t>(raw_displacement), target)) {
            return StepResult::kMemoryFault;
        }
        if (opcode == 0xE8U && !push_u64(state, memory, return_address)) {
            return StepResult::kMemoryFault;
        }
        state.rip = target;
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    return StepResult::kUnsupportedInstruction;
}

StepResult Interpreter::run(
    CpuState& state,
    memory::GuestMemory& memory,
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
