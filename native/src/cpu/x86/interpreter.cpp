#include "astrawin/cpu/x86/interpreter.h"

#include <cstdint>
#include <limits>

namespace astrawin::cpu::x86 {
namespace {

constexpr std::uint32_t kZeroFlag = 1U << 6U;
constexpr std::uint32_t kStackRegister = 4U;

bool fetch_u8(
    memory::GuestMemory& memory,
    std::uint32_t& eip,
    std::uint8_t& value) noexcept {
    if (!memory.read_value(eip, value)) {
        return false;
    }
    ++eip;
    return true;
}

bool fetch_u16(
    memory::GuestMemory& memory,
    std::uint32_t& eip,
    std::uint16_t& value) noexcept {
    if (!memory.read_value(eip, value)) {
        return false;
    }
    eip += sizeof(value);
    return true;
}

bool fetch_u32(
    memory::GuestMemory& memory,
    std::uint32_t& eip,
    std::uint32_t& value) noexcept {
    if (!memory.read_value(eip, value)) {
        return false;
    }
    eip += sizeof(value);
    return true;
}

bool push_u32(
    CpuState& state,
    memory::GuestMemory& memory,
    std::uint32_t value) noexcept {
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

bool pop_u32(
    CpuState& state,
    memory::GuestMemory& memory,
    std::uint32_t& value,
    std::uint16_t stack_adjustment = 0U) noexcept {
    const auto stack_pointer = state.gpr[kStackRegister];
    const auto increment = static_cast<std::uint64_t>(sizeof(value)) +
                           stack_adjustment;
    if (increment > std::numeric_limits<std::uint32_t>::max() - stack_pointer) {
        return false;
    }
    if (!memory.read_value(stack_pointer, value)) {
        return false;
    }
    state.gpr[kStackRegister] += static_cast<std::uint32_t>(increment);
    return true;
}

bool decode_memory_address32(
    memory::GuestMemory& memory,
    std::uint32_t& eip,
    const std::array<std::uint32_t, 8>& registers,
    std::uint8_t modrm,
    std::uint32_t& address) noexcept {
    const auto mod = static_cast<std::uint8_t>(modrm >> 6U);
    const auto rm = static_cast<std::uint8_t>(modrm & 0x7U);
    if (mod == 3U) {
        return false;
    }

    address = 0U;
    if (rm == 4U) {
        std::uint8_t sib = 0U;
        if (!fetch_u8(memory, eip, sib)) {
            return false;
        }
        const auto scale = static_cast<std::uint8_t>(sib >> 6U);
        const auto index = static_cast<std::uint8_t>((sib >> 3U) & 0x7U);
        const auto base = static_cast<std::uint8_t>(sib & 0x7U);
        if (index != 4U) {
            address += registers[index] << scale;
        }
        if (mod == 0U && base == 5U) {
            std::uint32_t displacement = 0U;
            if (!fetch_u32(memory, eip, displacement)) {
                return false;
            }
            address += displacement;
        } else {
            address += registers[base];
        }
    } else if (mod == 0U && rm == 5U) {
        if (!fetch_u32(memory, eip, address)) {
            return false;
        }
    } else {
        address = registers[rm];
    }

    if (mod == 1U) {
        std::uint8_t raw_displacement = 0U;
        if (!fetch_u8(memory, eip, raw_displacement)) {
            return false;
        }
        address += static_cast<std::uint32_t>(
            static_cast<std::int32_t>(
                static_cast<std::int8_t>(raw_displacement)));
    } else if (mod == 2U) {
        std::uint32_t displacement = 0U;
        if (!fetch_u32(memory, eip, displacement)) {
            return false;
        }
        address += displacement;
    }
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
    memory::GuestMemory& memory) const noexcept {
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

    if (opcode == 0xF4U) {
        state.halted = true;
        ++state.instructions_executed;
        return StepResult::kHalted;
    }

    if (opcode >= 0x50U && opcode <= 0x57U) { // PUSH r32
        const auto register_index = static_cast<std::size_t>(opcode - 0x50U);
        const auto value = state.gpr[register_index];
        if (!push_u32(state, memory, value)) {
            return StepResult::kMemoryFault;
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode >= 0x58U && opcode <= 0x5FU) { // POP r32
        const auto register_index = static_cast<std::size_t>(opcode - 0x58U);
        std::uint32_t value = 0;
        if (!pop_u32(state, memory, value)) {
            return StepResult::kMemoryFault;
        }
        // POP ESP replaces the incremented stack pointer with the popped value.
        state.gpr[register_index] = value;
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0x68U) { // PUSH imm32
        std::uint32_t immediate = 0;
        if (!fetch_u32(memory, state.eip, immediate) ||
            !push_u32(state, memory, immediate)) {
            return StepResult::kMemoryFault;
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0x6AU) { // PUSH imm8 (sign-extended)
        std::uint8_t raw_immediate = 0;
        if (!fetch_u8(memory, state.eip, raw_immediate)) {
            return StepResult::kMemoryFault;
        }
        const auto immediate = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int8_t>(raw_immediate)));
        if (!push_u32(state, memory, immediate)) {
            return StepResult::kMemoryFault;
        }
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xC3U || opcode == 0xC2U) { // RET / RET imm16
        std::uint16_t stack_adjustment = 0U;
        if (opcode == 0xC2U &&
            !fetch_u16(memory, state.eip, stack_adjustment)) {
            return StepResult::kMemoryFault;
        }
        std::uint32_t return_address = 0;
        if (!pop_u32(state, memory, return_address, stack_adjustment)) {
            return StepResult::kMemoryFault;
        }
        state.eip = return_address;
        ++state.instructions_executed;
        return StepResult::kRunning;
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

    if (opcode == 0xE9U || opcode == 0xE8U) { // JMP rel32 / CALL rel32
        std::uint32_t raw_displacement = 0;
        if (!fetch_u32(memory, state.eip, raw_displacement)) {
            return StepResult::kMemoryFault;
        }
        const auto return_address = state.eip;
        const auto target = static_cast<std::uint32_t>(
            static_cast<std::int64_t>(return_address) +
            static_cast<std::int32_t>(raw_displacement));
        if (opcode == 0xE8U && !push_u32(state, memory, return_address)) {
            return StepResult::kMemoryFault;
        }
        state.eip = target;
        ++state.instructions_executed;
        return StepResult::kRunning;
    }

    if (opcode == 0xFFU) { // CALL/JMP indirect r/m32
        std::uint8_t modrm = 0U;
        if (!fetch_u8(memory, state.eip, modrm)) {
            return StepResult::kMemoryFault;
        }
        const auto operation = static_cast<std::uint8_t>((modrm >> 3U) & 0x7U);
        if (operation != 2U && operation != 4U) {
            return StepResult::kUnsupportedInstruction;
        }

        const auto mod = static_cast<std::uint8_t>(modrm >> 6U);
        const auto rm = static_cast<std::uint8_t>(modrm & 0x7U);
        std::uint32_t target = 0U;
        if (mod == 3U) {
            target = state.gpr[rm];
        } else {
            std::uint32_t operand_address = 0U;
            if (!decode_memory_address32(
                    memory, state.eip, state.gpr, modrm, operand_address) ||
                !memory.read_value(operand_address, target)) {
                return StepResult::kMemoryFault;
            }
        }

        const auto return_address = state.eip;
        if (operation == 2U &&
            !push_u32(state, memory, return_address)) {
            return StepResult::kMemoryFault;
        }
        state.eip = target;
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

}  // namespace astrawin::cpu::x86
