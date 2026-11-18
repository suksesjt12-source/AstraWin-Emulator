#include "astrawin/pe/pe_loader.h"
#include "astrawin/win32/api_registry.h"
#include "astrawin/win32/environment.h"
#include "astrawin/win32/call_dispatcher.h"
#include "astrawin/process/guest_process.h"
#include "astrawin/filesystem/guest_path_mapper.h"
#include "astrawin/sync/guest_event.h"
#include "astrawin/sync/guest_timer.h"
#include "astrawin/graphics/headless_device.h"
#include <algorithm>
#include <thread>
#include <filesystem>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "astrawin/cpu/x86_64/interpreter.h"
#include "astrawin/cpu/x86/interpreter.h"
#include "astrawin/memory/guest_memory.h"
#include "astrawin/pe/pe_parser.h"

namespace {

bool test_guest_memory() {
    astrawin::memory::GuestMemory memory(16);
    const std::uint32_t source = 0x12345678U;
    std::uint32_t destination = 0;
    if (!memory.write_value(4, source) ||
        !memory.read_value(4, destination) ||
        destination != source) {
        return false;
    }

    std::uint64_t out_of_range = 0;
    if (memory.read_value(12, out_of_range)) {
        return false;
    }

    std::vector<std::uint8_t> image_bytes{0x90U, 0xF4U, 0xCCU};
    astrawin::memory::GuestMemory moved_memory(std::move(image_bytes));
    std::uint8_t first = 0;
    std::uint8_t last = 0;
    if (moved_memory.size() != 3U ||
        !moved_memory.read_value(0U, first) || first != 0x90U ||
        !moved_memory.read_value(2U, last) || last != 0xCCU) {
        return false;
    }

    astrawin::memory::GuestMemory based_memory(4U, 0x1000U);
    const std::uint8_t value = 0x5AU;
    std::uint8_t loaded_value = 0;
    if (based_memory.base_address() != 0x1000U ||
        based_memory.write_value(0x0FFFU, value) ||
        !based_memory.write_value(0x1002U, value) ||
        !based_memory.read_value(0x1002U, loaded_value) ||
        loaded_value != value ||
        based_memory.read_value(0x0FFFU, loaded_value)) {
        return false;
    }

    try {
        astrawin::memory::GuestMemory overflowing_memory(
            4U, std::numeric_limits<std::uint64_t>::max() - 1U);
        return false;
    } catch (const std::length_error&) {
        return true;
    }
}

bool test_x64_interpreter() {
    using astrawin::cpu::x86_64::CpuState;
    using astrawin::cpu::x86_64::Interpreter;
    using astrawin::cpu::x86_64::StepResult;

    astrawin::memory::GuestMemory memory(64);
    const std::vector<std::uint8_t> program{
        0x48U, 0xB8U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x48U, 0x05U, 0x05U, 0x00U, 0x00U, 0x00U,
        0xF4U
    };
    if (!memory.write(0, program)) {
        return false;
    }

    CpuState state{};
    const auto result = Interpreter{}.run(state, memory, 16);
    if (result != StepResult::kHalted ||
        state.gpr[0] != 12U ||
        state.instructions_executed != 3U) {
        return false;
    }

    astrawin::memory::GuestMemory ret_memory(4);
    const std::uint8_t ret_opcode = 0xC3U;
    if (!ret_memory.write_value(0, ret_opcode)) {
        return false;
    }
    CpuState ret_state{};
    ret_state.gpr[4] = 4U; // Empty stack points just past the guest memory.
    return Interpreter{}.step(ret_state, ret_memory) ==
               StepResult::kMemoryFault &&
           !ret_state.halted;
}


bool test_x86_interpreter() {
    using astrawin::cpu::x86::CpuState;
    using astrawin::cpu::x86::Interpreter;
    using astrawin::cpu::x86::StepResult;

    astrawin::memory::GuestMemory memory(64);
    const std::vector<std::uint8_t> program{
        0xB8U, 0x06U, 0x00U, 0x00U, 0x00U,
        0x05U, 0x01U, 0x00U, 0x00U, 0x00U,
        0xF4U
    };
    if (!memory.write(0, program)) {
        return false;
    }

    CpuState state{};
    const auto result = Interpreter{}.run(state, memory, 16);
    if (result != StepResult::kHalted ||
        state.gpr[0] != 7U ||
        state.instructions_executed != 3U) {
        return false;
    }

    astrawin::memory::GuestMemory ret_memory(4);
    const std::uint8_t ret_opcode = 0xC3U;
    if (!ret_memory.write_value(0, ret_opcode)) {
        return false;
    }
    CpuState ret_state{};
    ret_state.gpr[4] = 4U; // Empty stack points just past the guest memory.
    return Interpreter{}.step(ret_state, ret_memory) ==
               StepResult::kMemoryFault &&
           !ret_state.halted;
}


bool test_x86_stack_and_call() {
    using astrawin::cpu::x86::CpuState;
    using astrawin::cpu::x86::Interpreter;
    using astrawin::cpu::x86::StepResult;

    constexpr std::uint32_t base = 0x1000U;
    constexpr std::uint32_t stack_top = base + 128U;
    astrawin::memory::GuestMemory memory(128U, base);
    std::vector<std::uint8_t> code(128U, 0x90U);
    code[0] = 0xE8U; // CALL rel32
    code[1] = 0x0BU;
    code[2] = 0U;
    code[3] = 0U;
    code[4] = 0U;
    code[5] = 0xF4U; // caller resumes here and halts
    code[16] = 0xB8U; // MOV EAX, 42
    code[17] = 42U;
    code[18] = 0U;
    code[19] = 0U;
    code[20] = 0U;
    code[21] = 0xC3U; // RET
    if (!memory.write(base, code)) {
        return false;
    }

    CpuState state{};
    state.eip = base;
    state.gpr[4] = stack_top;
    const auto result = Interpreter{}.run(state, memory, 8U);
    if (result != StepResult::kHalted || state.gpr[0] != 42U ||
        state.gpr[4] != stack_top || state.eip != base + 6U ||
        state.instructions_executed != 4U) {
        return false;
    }

    astrawin::memory::GuestMemory stack_memory(128U, base);
    const std::vector<std::uint8_t> stack_program{
        0x6AU, 0x80U,       // PUSH -128
        0x58U,              // POP EAX
        0x50U,              // PUSH EAX
        0x5BU,              // POP EBX
        0xF4U
    };
    if (!stack_memory.write(base, stack_program)) {
        return false;
    }
    CpuState stack_state{};
    stack_state.eip = base;
    stack_state.gpr[4] = stack_top;
    const auto stack_result = Interpreter{}.run(stack_state, stack_memory, 8U);
    if (stack_result != StepResult::kHalted ||
        stack_state.gpr[0] != 0xFFFFFF80U ||
        stack_state.gpr[3] != 0xFFFFFF80U ||
        stack_state.gpr[4] != stack_top) {
        return false;
    }

    astrawin::memory::GuestMemory ret_adjust_memory(128U, base);
    std::vector<std::uint8_t> ret_adjust_code(128U, 0x90U);
    ret_adjust_code[0] = 0x68U; // PUSH 0x1234 as a caller argument
    ret_adjust_code[1] = 0x34U;
    ret_adjust_code[2] = 0x12U;
    ret_adjust_code[3] = 0U;
    ret_adjust_code[4] = 0U;
    ret_adjust_code[5] = 0xE8U; // CALL function at offset 0x20
    ret_adjust_code[6] = 0x16U;
    ret_adjust_code[7] = 0U;
    ret_adjust_code[8] = 0U;
    ret_adjust_code[9] = 0U;
    ret_adjust_code[10] = 0xF4U;
    ret_adjust_code[32] = 0xC2U; // RET 4 (one 32-bit stack argument)
    ret_adjust_code[33] = 0x04U;
    ret_adjust_code[34] = 0U;
    if (!ret_adjust_memory.write(base, ret_adjust_code)) {
        return false;
    }
    CpuState ret_adjust_state{};
    ret_adjust_state.eip = base;
    ret_adjust_state.gpr[4] = stack_top;
    const auto ret_adjust_result =
        Interpreter{}.run(ret_adjust_state, ret_adjust_memory, 8U);
    if (ret_adjust_result != StepResult::kHalted ||
        ret_adjust_state.gpr[4] != stack_top) {
        return false;
    }

    CpuState push_fault{};
    push_fault.eip = base;
    push_fault.gpr[4] = base;
    if (Interpreter{}.step(push_fault, stack_memory) != StepResult::kMemoryFault ||
        push_fault.gpr[4] != base) {
        return false;
    }
    CpuState pop_fault{};
    pop_fault.eip = base + 2U; // POP EAX
    pop_fault.gpr[4] = stack_top;
    return Interpreter{}.step(pop_fault, stack_memory) == StepResult::kMemoryFault &&
           pop_fault.gpr[4] == stack_top;
}

bool test_x64_stack_and_call() {
    using astrawin::cpu::x86_64::CpuState;
    using astrawin::cpu::x86_64::Interpreter;
    using astrawin::cpu::x86_64::StepResult;

    constexpr std::uint64_t base = 0x100000U;
    constexpr std::uint64_t stack_top = base + 256U;
    astrawin::memory::GuestMemory memory(256U, base);
    std::vector<std::uint8_t> code(256U, 0x90U);
    code[0] = 0xE8U; // CALL rel32
    code[1] = 0x1BU;
    code[2] = 0U;
    code[3] = 0U;
    code[4] = 0U;
    code[5] = 0xF4U;
    code[32] = 0x48U; // MOV RAX, 0x1122334455667788
    code[33] = 0xB8U;
    const std::uint64_t expected_rax = 0x1122334455667788ULL;
    for (std::size_t index = 0; index < 8U; ++index) {
        code[34U + index] = static_cast<std::uint8_t>(
            (expected_rax >> (index * 8U)) & 0xFFU);
    }
    code[42] = 0xC3U;
    if (!memory.write(base, code)) {
        return false;
    }

    CpuState state{};
    state.rip = base;
    state.gpr[4] = stack_top;
    const auto result = Interpreter{}.run(state, memory, 8U);
    if (result != StepResult::kHalted || state.gpr[0] != expected_rax ||
        state.gpr[4] != stack_top || state.rip != base + 6U ||
        state.instructions_executed != 4U) {
        return false;
    }

    astrawin::memory::GuestMemory stack_memory(256U, base);
    const std::vector<std::uint8_t> stack_program{
        0x41U, 0x50U,       // PUSH R8
        0x41U, 0x59U,       // POP R9
        0x6AU, 0x80U,       // PUSH sign-extended -128
        0x58U,              // POP RAX
        0xF4U
    };
    if (!stack_memory.write(base, stack_program)) {
        return false;
    }
    CpuState stack_state{};
    stack_state.rip = base;
    stack_state.gpr[4] = stack_top;
    stack_state.gpr[8] = 0x8877665544332211ULL;
    const auto stack_result = Interpreter{}.run(stack_state, stack_memory, 10U);
    if (stack_result != StepResult::kHalted ||
        stack_state.gpr[9] != 0x8877665544332211ULL ||
        stack_state.gpr[0] != 0xFFFFFFFFFFFFFF80ULL ||
        stack_state.gpr[4] != stack_top) {
        return false;
    }

    astrawin::memory::GuestMemory ret_adjust_memory(256U, base);
    std::vector<std::uint8_t> ret_adjust_code(256U, 0x90U);
    ret_adjust_code[0] = 0x68U; // PUSH 0x1234 as a caller argument
    ret_adjust_code[1] = 0x34U;
    ret_adjust_code[2] = 0x12U;
    ret_adjust_code[3] = 0U;
    ret_adjust_code[4] = 0U;
    ret_adjust_code[5] = 0xE8U;
    ret_adjust_code[6] = 0x16U;
    ret_adjust_code[7] = 0U;
    ret_adjust_code[8] = 0U;
    ret_adjust_code[9] = 0U;
    ret_adjust_code[10] = 0xF4U;
    ret_adjust_code[32] = 0xC2U; // RET 8 (one 64-bit stack argument)
    ret_adjust_code[33] = 0x08U;
    ret_adjust_code[34] = 0U;
    if (!ret_adjust_memory.write(base, ret_adjust_code)) {
        return false;
    }
    CpuState ret_adjust_state{};
    ret_adjust_state.rip = base;
    ret_adjust_state.gpr[4] = stack_top;
    const auto ret_adjust_result =
        Interpreter{}.run(ret_adjust_state, ret_adjust_memory, 8U);
    if (ret_adjust_result != StepResult::kHalted ||
        ret_adjust_state.gpr[4] != stack_top) {
        return false;
    }

    CpuState push_fault{};
    push_fault.rip = base;
    push_fault.gpr[4] = base;
    if (Interpreter{}.step(push_fault, stack_memory) != StepResult::kMemoryFault ||
        push_fault.gpr[4] != base) {
        return false;
    }
    CpuState pop_fault{};
    pop_fault.rip = base + 2U; // POP R9 with REX.B
    pop_fault.gpr[4] = stack_top;
    return Interpreter{}.step(pop_fault, stack_memory) == StepResult::kMemoryFault &&
           pop_fault.gpr[4] == stack_top;
}

bool test_pe32_plus_inspection() {
    std::vector<std::uint8_t> bytes(512, 0);
    const auto put16 = [&bytes](std::size_t offset, std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    };
    const auto put32 = [&bytes](std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4U; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    };
    const auto put64 = [&bytes](std::size_t offset, std::uint64_t value) {
        for (std::size_t i = 0; i < 8U; ++i) {
            bytes[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    };

    constexpr std::size_t pe = 0x80U;
    constexpr std::size_t optional = pe + 24U;
    put16(0, 0x5A4DU);
    put32(0x3CU, static_cast<std::uint32_t>(pe));
    put32(pe, 0x00004550U);
    put16(pe + 4U, 0x8664U);
    put16(pe + 6U, 3U);
    put16(pe + 20U, 0xF0U);
    put16(optional, 0x020BU);
    put32(optional + 16U, 0x1000U);
    put64(optional + 24U, 0x0000000140000000ULL);

    astrawin::pe::PeImageInfo info{};
    std::string error;
    const bool parsed = astrawin::pe::PeParser::inspect(bytes, info, error);
    auto truncated_optional = bytes;
    truncated_optional[pe + 20U] = 31U;
    truncated_optional[pe + 21U] = 0U;
    const bool invalid_optional_rejected =
        !astrawin::pe::PeParser::inspect(truncated_optional, info, error);

    const auto put16_in = [](
        std::vector<std::uint8_t>& target,
        std::size_t offset,
        std::uint16_t value) {
        target[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        target[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    };
    const auto put32_in = [](
        std::vector<std::uint8_t>& target,
        std::size_t offset,
        std::uint32_t value) {
        for (std::size_t i = 0; i < 4U; ++i) {
            target[offset + i] =
                static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    };

    auto pe32 = bytes;
    put16_in(pe32, pe + 4U, 0x014CU);
    put16_in(pe32, optional, 0x010BU);
    put32_in(pe32, optional + 28U, 0x00400000U);
    astrawin::pe::PeImageInfo pe32_info{};
    std::string pe32_error;
    const bool pe32_parsed =
        astrawin::pe::PeParser::inspect(pe32, pe32_info, pe32_error) &&
        pe32_info.architecture == astrawin::pe::PeArchitecture::kX86 &&
        !pe32_info.is_64_bit &&
        pe32_info.image_base == 0x00400000U;

    auto mismatched_format = bytes;
    put16_in(mismatched_format, pe + 4U, 0x014CU);
    std::string mismatch_error;
    const bool mismatched_format_rejected =
        !astrawin::pe::PeParser::inspect(
            mismatched_format, pe32_info, mismatch_error);

    return parsed && invalid_optional_rejected && pe32_parsed &&
           mismatched_format_rejected &&
           info.architecture == astrawin::pe::PeArchitecture::kX64 &&
           info.is_64_bit &&
           info.section_count == 3U &&
           info.entry_point_rva == 0x1000U &&
           info.image_base == 0x0000000140000000ULL;
}

bool test_pe_section_mapping() {
    std::vector<std::uint8_t> file(0x204U, 0U);
    const auto put16 = [&file](std::size_t offset, std::uint16_t value) {
        file[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        file[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    };
    const auto put32 = [&file](std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4U; ++i) {
            file[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    };
    const auto put64 = [&file](std::size_t offset, std::uint64_t value) {
        for (std::size_t i = 0; i < 8U; ++i) {
            file[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    };

    constexpr std::size_t pe = 0x80U;
    constexpr std::size_t optional = pe + 24U;
    constexpr std::size_t section = optional + 0xF0U;
    put16(0, 0x5A4DU);
    put32(0x3CU, static_cast<std::uint32_t>(pe));
    put32(pe, 0x00004550U);
    put16(pe + 4U, 0x8664U);
    put16(pe + 6U, 1U);
    put16(pe + 20U, 0xF0U);
    put16(optional, 0x020BU);
    put32(optional + 16U, 0x1000U);
    put64(optional + 24U, 0x0000000140000000ULL);
    put32(optional + 56U, 0x2000U);
    put32(optional + 60U, 0x200U);

    file[section] = '.';
    file[section + 1U] = 't';
    file[section + 2U] = 'e';
    file[section + 3U] = 'x';
    file[section + 4U] = 't';
    put32(section + 8U, 16U);
    put32(section + 12U, 0x1000U);
    put32(section + 16U, 4U);
    put32(section + 20U, 0x200U);
    put32(section + 36U, 0x60000020U);
    file[0x200U] = 0x90U;
    file[0x201U] = 0x90U;
    file[0x202U] = 0xF4U;
    file[0x203U] = 0xCCU;

    astrawin::pe::LoadedPeImage loaded{};
    std::string error;
    if (!astrawin::pe::PeImageLoader::load(file, loaded, error)) {
        return false;
    }
    const bool mapped =
        loaded.bytes.size() == 0x2000U &&
        loaded.sections.size() == 1U &&
        loaded.sections[0].name == ".text" &&
        loaded.sections[0].virtual_address == 0x1000U &&
        loaded.sections[0].mapped_size == 16U &&
        loaded.bytes[0x1000U] == 0x90U &&
        loaded.bytes[0x1002U] == 0xF4U &&
        loaded.bytes[0x1004U] == 0U;

    auto overlap_file = file;
    overlap_file[pe + 6U] = 2U;
    const auto second_section = section + 40U;
    overlap_file[second_section] = '.';
    overlap_file[second_section + 1U] = 'd';
    overlap_file[second_section + 2U] = 'a';
    overlap_file[second_section + 3U] = 't';
    overlap_file[second_section + 4U] = 'a';
    const auto put32_overlap = [&overlap_file](std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4U; ++i) {
            overlap_file[offset + i] =
                static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    };
    put32_overlap(second_section + 8U, 8U);
    put32_overlap(second_section + 12U, 0x1008U);
    put32_overlap(second_section + 36U, 0xC0000040U);
    astrawin::pe::LoadedPeImage overlapping{};
    std::string overlapping_error;
    const bool overlap_rejected =
        !astrawin::pe::PeImageLoader::load(
            overlap_file, overlapping, overlapping_error);

    auto raw_overlap_file = file;
    raw_overlap_file[pe + 6U] = 2U;
    const auto raw_overlap_section = section + 40U;
    raw_overlap_file[raw_overlap_section] = '.';
    raw_overlap_file[raw_overlap_section + 1U] = 'd';
    raw_overlap_file[raw_overlap_section + 2U] = 'a';
    raw_overlap_file[raw_overlap_section + 3U] = 't';
    raw_overlap_file[raw_overlap_section + 4U] = 'a';
    const auto put32_raw_overlap = [&raw_overlap_file](
        std::size_t offset, std::uint32_t value) {
        for (std::size_t i = 0; i < 4U; ++i) {
            raw_overlap_file[offset + i] =
                static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
        }
    };
    put32_raw_overlap(raw_overlap_section + 8U, 4U);
    put32_raw_overlap(raw_overlap_section + 12U, 0x1100U);
    put32_raw_overlap(raw_overlap_section + 16U, 2U);
    put32_raw_overlap(raw_overlap_section + 20U, 0x202U);
    put32_raw_overlap(raw_overlap_section + 36U, 0x40000040U);
    astrawin::pe::LoadedPeImage raw_overlapping{};
    std::string raw_overlap_error;
    const bool raw_overlap_rejected =
        !astrawin::pe::PeImageLoader::load(
            raw_overlap_file, raw_overlapping, raw_overlap_error);

    auto non_executable_file = file;
    non_executable_file[section + 36U] = 0x40U;
    non_executable_file[section + 37U] = 0x00U;
    non_executable_file[section + 38U] = 0x00U;
    non_executable_file[section + 39U] = 0x40U;
    astrawin::pe::LoadedPeImage non_executable{};
    std::string non_executable_error;
    const bool non_executable_rejected =
        !astrawin::pe::PeImageLoader::load(
            non_executable_file, non_executable, non_executable_error);

    put32(section + 20U, 0x210U);
    astrawin::pe::LoadedPeImage rejected{};
    std::string rejected_error;
    const bool malformed_rejected =
        !astrawin::pe::PeImageLoader::load(file, rejected, rejected_error);
    return mapped && malformed_rejected &&
           overlap_rejected && raw_overlap_rejected &&
           non_executable_rejected;
}



bool test_pe_relocations_and_imports() {
    constexpr std::size_t kPeOffset = 0x80U;
    constexpr std::size_t kOptionalOffset = kPeOffset + 24U;
    constexpr std::size_t kSectionTable = kOptionalOffset + 0xF0U;
    constexpr std::uint64_t kPreferredBase64 = 0x0000000140000000ULL;
    constexpr std::uint32_t kPreferredBase32 = 0x00400000U;

    const auto put16 = [](std::vector<std::uint8_t>& bytes,
                          std::size_t offset,
                          std::uint16_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    };
    const auto put32 = [](std::vector<std::uint8_t>& bytes,
                          std::size_t offset,
                          std::uint32_t value) {
        for (std::size_t index = 0; index < 4U; ++index) {
            bytes[offset + index] =
                static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
        }
    };
    const auto put64 = [](std::vector<std::uint8_t>& bytes,
                          std::size_t offset,
                          std::uint64_t value) {
        for (std::size_t index = 0; index < 8U; ++index) {
            bytes[offset + index] =
                static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
        }
    };
    const auto get32 = [](const std::vector<std::uint8_t>& bytes,
                          std::size_t offset) {
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4U; ++index) {
            value |= static_cast<std::uint32_t>(bytes[offset + index])
                     << (index * 8U);
        }
        return value;
    };
    const auto get64 = [](const std::vector<std::uint8_t>& bytes,
                          std::size_t offset) {
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8U; ++index) {
            value |= static_cast<std::uint64_t>(bytes[offset + index])
                     << (index * 8U);
        }
        return value;
    };

    const auto make_pe = [&](bool is_64_bit,
                             bool include_relocations,
                             bool include_imports) {
        std::vector<std::uint8_t> file(0x600U, 0U);
        put16(file, 0U, 0x5A4DU);
        put32(file, 0x3CU, static_cast<std::uint32_t>(kPeOffset));
        put32(file, kPeOffset, 0x00004550U);
        put16(file, kPeOffset + 4U, is_64_bit ? 0x8664U : 0x014CU);
        put16(file, kPeOffset + 6U, 2U);
        put16(file, kPeOffset + 20U, 0xF0U);
        put16(file, kOptionalOffset, is_64_bit ? 0x020BU : 0x010BU);
        put32(file, kOptionalOffset + 16U, 0x1000U);
        const std::uint64_t preferred_base =
            is_64_bit ? kPreferredBase64 : kPreferredBase32;
        if (is_64_bit) {
            put64(file, kOptionalOffset + 24U, preferred_base);
            put32(file, kOptionalOffset + 108U, 16U);
        } else {
            put32(file, kOptionalOffset + 28U,
                  static_cast<std::uint32_t>(preferred_base));
            put32(file, kOptionalOffset + 92U, 16U);
        }
        put32(file, kOptionalOffset + 56U, 0x3000U);
        put32(file, kOptionalOffset + 60U, 0x200U);

        const auto directory_table = kOptionalOffset + (is_64_bit ? 112U : 96U);
        if (include_imports) {
            put32(file, directory_table + 8U, 0x2000U);
            put32(file, directory_table + 12U, 40U);
        }
        if (include_relocations) {
            put32(file, directory_table + 5U * 8U, 0x2100U);
            put32(file, directory_table + 5U * 8U + 4U, 12U);
        }

        file[kSectionTable] = '.';
        file[kSectionTable + 1U] = 't';
        file[kSectionTable + 2U] = 'e';
        file[kSectionTable + 3U] = 'x';
        file[kSectionTable + 4U] = 't';
        put32(file, kSectionTable + 8U, 0x200U);
        put32(file, kSectionTable + 12U, 0x1000U);
        put32(file, kSectionTable + 16U, 0x200U);
        put32(file, kSectionTable + 20U, 0x200U);
        put32(file, kSectionTable + 36U, 0x60000020U);

        const auto data_section = kSectionTable + 40U;
        file[data_section] = '.';
        file[data_section + 1U] = 'd';
        file[data_section + 2U] = 'a';
        file[data_section + 3U] = 't';
        file[data_section + 4U] = 'a';
        put32(file, data_section + 8U, 0x200U);
        put32(file, data_section + 12U, 0x2000U);
        put32(file, data_section + 16U, 0x200U);
        put32(file, data_section + 20U, 0x400U);
        put32(file, data_section + 36U, 0xC0000040U);

        if (is_64_bit) {
            put64(file, 0x208U, kPreferredBase64 + 0x1234U);
        } else {
            put32(file, 0x208U, kPreferredBase32 + 0x1234U);
        }

        if (include_relocations) {
            const std::size_t relocation_file_offset = 0x500U;
            put32(file, relocation_file_offset, 0x1000U);
            put32(file, relocation_file_offset + 4U, 12U);
            put16(file, relocation_file_offset + 8U,
                  is_64_bit ? 0xA008U : 0x3008U);
            put16(file, relocation_file_offset + 10U, 0U);
        }

        if (include_imports) {
            constexpr std::size_t data_raw = 0x400U;
            put32(file, data_raw, 0x2050U);       // OriginalFirstThunk
            put32(file, data_raw + 12U, 0x2040U); // Name
            put32(file, data_raw + 16U, 0x2060U); // FirstThunk

            const std::string module_name = "KERNEL32.dll";
            std::copy(module_name.begin(), module_name.end(),
                      file.begin() + static_cast<std::ptrdiff_t>(data_raw + 0x40U));
            const std::string import_name = "ExitProcess";
            std::copy(import_name.begin(), import_name.end(),
                      file.begin() + static_cast<std::ptrdiff_t>(data_raw + 0x72U));
            if (is_64_bit) {
                put64(file, data_raw + 0x50U, 0x2070U);
                put64(file, data_raw + 0x58U, 0U);
            } else {
                put32(file, data_raw + 0x50U, 0x2070U);
                put32(file, data_raw + 0x54U, 0U);
            }
            put16(file, data_raw + 0x70U, 0U); // Hint
        }
        return file;
    };

    auto relocation64_file = make_pe(true, true, false);
    astrawin::pe::PeLoadOptions relocation64_options{};
    relocation64_options.load_base = kPreferredBase64 + 0x100000U;
    astrawin::pe::LoadedPeImage relocated64{};
    std::string relocation64_error;
    const bool relocation64_ok = astrawin::pe::PeImageLoader::load(
        relocation64_file, relocated64, relocation64_error, relocation64_options);
    const bool relocation64_value_ok = relocation64_ok &&
        relocated64.info.image_base == kPreferredBase64 + 0x100000U &&
        relocated64.preferred_image_base == kPreferredBase64 &&
        relocated64.relocations_applied &&
        get64(relocated64.bytes, 0x1008U) ==
            kPreferredBase64 + 0x100000U + 0x1234U;

    auto relocation32_file = make_pe(false, true, false);
    astrawin::pe::PeLoadOptions relocation32_options{};
    relocation32_options.load_base =
        static_cast<std::uint64_t>(kPreferredBase32) + 0x100000U;
    astrawin::pe::LoadedPeImage relocated32{};
    std::string relocation32_error;
    const bool relocation32_ok = astrawin::pe::PeImageLoader::load(
        relocation32_file, relocated32, relocation32_error, relocation32_options);
    const bool relocation32_value_ok = relocation32_ok &&
        relocated32.info.image_base ==
            static_cast<std::uint64_t>(kPreferredBase32) + 0x100000U &&
        relocated32.relocations_applied &&
        get32(relocated32.bytes, 0x1008U) ==
            kPreferredBase32 + 0x100000U + 0x1234U;

    auto missing_relocation_file = make_pe(true, false, false);
    astrawin::pe::LoadedPeImage missing_relocation_image{};
    std::string missing_relocation_error;
    const bool missing_relocation_rejected = !astrawin::pe::PeImageLoader::load(
        missing_relocation_file, missing_relocation_image,
        missing_relocation_error, relocation64_options);

    auto malformed_relocation_file = make_pe(true, true, false);
    put32(malformed_relocation_file, 0x504U, 7U);
    astrawin::pe::LoadedPeImage malformed_relocation_image{};
    std::string malformed_relocation_error;
    const bool malformed_relocation_rejected = !astrawin::pe::PeImageLoader::load(
        malformed_relocation_file, malformed_relocation_image,
        malformed_relocation_error, relocation64_options);

    auto empty_relocation_file = make_pe(true, true, false);
    put16(empty_relocation_file, 0x508U, 0U);
    astrawin::pe::LoadedPeImage empty_relocation_image{};
    std::string empty_relocation_error;
    const bool empty_relocation_rejected = !astrawin::pe::PeImageLoader::load(
        empty_relocation_file, empty_relocation_image,
        empty_relocation_error, relocation64_options);

    auto import_file = make_pe(true, false, true);
    astrawin::pe::LoadedPeImage unresolved_image{};
    std::string unresolved_error;
    const bool import_enumeration_ok = astrawin::pe::PeImageLoader::load(
        import_file, unresolved_image, unresolved_error) &&
        !unresolved_image.imports_resolved &&
        unresolved_image.imports.size() == 1U &&
        unresolved_image.imports[0].module == "KERNEL32.dll" &&
        unresolved_image.imports[0].name == "ExitProcess" &&
        !unresolved_image.imports[0].resolved;

    astrawin::pe::PeLoadOptions import_options{};
    import_options.import_resolver = [](
        const astrawin::pe::ImportedSymbol& symbol,
        std::uint64_t& address,
        std::string& resolver_error) {
        if (symbol.module != "KERNEL32.dll" ||
            symbol.name != "ExitProcess" || symbol.by_ordinal) {
            resolver_error = "Unexpected import symbol";
            return false;
        }
        address = 0x0000000177771234ULL;
        return true;
    };
    astrawin::pe::LoadedPeImage resolved_image{};
    std::string resolved_error;
    const bool import_resolution_ok = astrawin::pe::PeImageLoader::load(
        import_file, resolved_image, resolved_error, import_options) &&
        resolved_image.imports_resolved &&
        resolved_image.imports.size() == 1U &&
        resolved_image.imports[0].resolved &&
        resolved_image.imports[0].resolved_address == 0x0000000177771234ULL &&
        get64(resolved_image.bytes, 0x2060U) == 0x0000000177771234ULL;

    return relocation64_value_ok && relocation32_value_ok &&
           missing_relocation_rejected && malformed_relocation_rejected &&
           empty_relocation_rejected && import_enumeration_ok && import_resolution_ok;
}

bool test_win32_api_environment() {
    using astrawin::win32::ApiRegistry;
    using astrawin::win32::Environment;

    const auto set_last_error = ApiRegistry::resolve("KeRnEl32.DlL", "SetLastError");
    const auto get_last_error = ApiRegistry::resolve("kernelbase.dll", "GetLastError");
    const auto get_pid = ApiRegistry::resolve("kernel32.dll", "GetCurrentProcessId");
    const auto get_tick_count = ApiRegistry::resolve("kernel32.dll", "GetTickCount64");
    const auto unknown = ApiRegistry::resolve("user32.dll", "MessageBoxW");
    if (!set_last_error || !get_last_error || !get_pid ||
        !get_tick_count || unknown) {
        return false;
    }

    Environment environment(4242U);
    std::uint64_t result = 0;
    const std::uint64_t error_argument = 5U;
    if (!environment.invoke(*set_last_error, std::span<const std::uint64_t>(&error_argument, 1U), result)) {
        return false;
    }
    if (!environment.invoke(*get_last_error, {}, result) || result != 5U) {
        return false;
    }
    if (!environment.invoke(*get_pid, {}, result) || result != 4242U) {
        return false;
    }
    if (!environment.invoke(*get_tick_count, {}, result)) {
        return false;
    }
    const auto first_tick_count = result;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    if (!environment.invoke(*get_tick_count, {}, result) ||
        result < first_tick_count) {
        return false;
    }
    const std::uint64_t unexpected_argument = 1U;
    if (environment.invoke(*get_pid, std::span<const std::uint64_t>(&unexpected_argument, 1U), result)) {
        return false;
    }
    return environment.last_error() == 87U;
}



bool test_win32_call_dispatcher() {
    using astrawin::win32::CallDispatcher;
    using astrawin::win32::Environment;
    using astrawin::win32::ApiId;
    using astrawin::pe::ImportedSymbol;

    CallDispatcher x86_dispatcher(false);
    std::uint64_t get_pid_thunk = 0U;
    std::uint64_t set_error_thunk = 0U;
    std::uint64_t get_ticks_thunk = 0U;
    std::string error;

    ImportedSymbol get_pid_import{};
    get_pid_import.module = "KERNEL32.dll";
    get_pid_import.name = "GetCurrentProcessId";
    ImportedSymbol set_error_import{};
    set_error_import.module = "kernelbase.dll";
    set_error_import.name = "SetLastError";
    ImportedSymbol get_ticks_import{};
    get_ticks_import.module = "kernel32.dll";
    get_ticks_import.name = "GetTickCount64";

    if (!x86_dispatcher.resolve_import(get_pid_import, get_pid_thunk, error) ||
        !x86_dispatcher.resolve_import(set_error_import, set_error_thunk, error) ||
        !x86_dispatcher.resolve_import(get_ticks_import, get_ticks_thunk, error) ||
        x86_dispatcher.thunk_count() != 3U ||
        x86_dispatcher.find_thunk(get_pid_thunk) != ApiId::kGetCurrentProcessId) {
        return false;
    }

    std::uint64_t duplicate_get_pid_thunk = 0U;
    if (!x86_dispatcher.resolve_import(
            get_pid_import, duplicate_get_pid_thunk, error) ||
        duplicate_get_pid_thunk != get_pid_thunk ||
        x86_dispatcher.thunk_count() != 3U) {
        return false;
    }

    ImportedSymbol unsupported_import{};
    unsupported_import.module = "user32.dll";
    unsupported_import.name = "MessageBoxW";
    std::uint64_t unsupported_address = 0U;
    if (x86_dispatcher.resolve_import(
            unsupported_import, unsupported_address, error)) {
        return false;
    }

    constexpr std::uint32_t x86_memory_base = 0x2000U;
    astrawin::memory::GuestMemory x86_memory(256U, x86_memory_base);
    Environment x86_environment(7654U);
    constexpr std::uint32_t x86_return_address = 0x12345678U;

    astrawin::cpu::x86::CpuState x86_state{};
    x86_state.eip = static_cast<std::uint32_t>(get_pid_thunk);
    x86_state.gpr[4] = x86_memory_base + 64U;
    if (!x86_memory.write_value(x86_state.gpr[4], x86_return_address)) {
        return false;
    }
    if (x86_dispatcher.step_x86(
            x86_state, x86_memory, x86_environment) !=
            astrawin::cpu::x86::StepResult::kRunning ||
        x86_state.eip != x86_return_address ||
        x86_state.gpr[4] != x86_memory_base + 68U ||
        x86_state.gpr[0] != 7654U ||
        x86_state.instructions_executed != 1U) {
        return false;
    }

    constexpr std::uint32_t x86_set_error_stack = x86_memory_base + 96U;
    constexpr std::uint32_t x86_set_error_return = 0x12345700U;
    constexpr std::uint32_t expected_last_error = 0x5A5AU;
    x86_state.eip = static_cast<std::uint32_t>(set_error_thunk);
    x86_state.gpr[4] = x86_set_error_stack;
    if (!x86_memory.write_value(x86_set_error_stack, x86_set_error_return) ||
        !x86_memory.write_value(
            x86_set_error_stack + 4U, expected_last_error)) {
        return false;
    }
    if (x86_dispatcher.step_x86(
            x86_state, x86_memory, x86_environment) !=
            astrawin::cpu::x86::StepResult::kRunning ||
        x86_state.eip != x86_set_error_return ||
        x86_state.gpr[4] != x86_set_error_stack + 8U ||
        x86_environment.last_error() != expected_last_error) {
        return false;
    }

    constexpr std::uint32_t x86_ticks_stack = x86_memory_base + 128U;
    x86_state.eip = static_cast<std::uint32_t>(get_ticks_thunk);
    x86_state.gpr[4] = x86_ticks_stack;
    if (!x86_memory.write_value(x86_ticks_stack, x86_return_address) ||
        x86_dispatcher.step_x86(
            x86_state, x86_memory, x86_environment) !=
            astrawin::cpu::x86::StepResult::kRunning ||
        x86_state.eip != x86_return_address ||
        x86_state.gpr[4] != x86_ticks_stack + 4U) {
        return false;
    }
    const auto x86_tick_count =
        (static_cast<std::uint64_t>(x86_state.gpr[2]) << 32U) |
        x86_state.gpr[0];
    if (x86_tick_count == 0U) {
        return false;
    }

    // Exercise the real x86 import-call sequence: CALL [absolute IAT slot],
    // thunk dispatch, return to caller, then execute the next instruction.
    constexpr std::uint32_t x86_iat_base = 0x4000U;
    constexpr std::uint32_t x86_iat_slot = x86_iat_base + 0x80U;
    astrawin::memory::GuestMemory x86_iat_memory(256U, x86_iat_base);
    std::vector<std::uint8_t> x86_iat_code(256U, 0x90U);
    x86_iat_code[0] = 0xFFU;
    x86_iat_code[1] = 0x15U; // CALL DWORD PTR [absolute address]
    x86_iat_code[2] = static_cast<std::uint8_t>(x86_iat_slot & 0xFFU);
    x86_iat_code[3] = static_cast<std::uint8_t>((x86_iat_slot >> 8U) & 0xFFU);
    x86_iat_code[4] = static_cast<std::uint8_t>((x86_iat_slot >> 16U) & 0xFFU);
    x86_iat_code[5] = static_cast<std::uint8_t>((x86_iat_slot >> 24U) & 0xFFU);
    x86_iat_code[6] = 0xF4U;
    if (!x86_iat_memory.write(x86_iat_base, x86_iat_code) ||
        !x86_iat_memory.write_value(
            x86_iat_slot, static_cast<std::uint32_t>(get_pid_thunk))) {
        return false;
    }
    astrawin::cpu::x86::CpuState x86_iat_state{};
    x86_iat_state.eip = x86_iat_base;
    x86_iat_state.gpr[4] = x86_iat_base + 256U;
    if (x86_dispatcher.run_x86(
            x86_iat_state, x86_iat_memory, x86_environment, 8U) !=
            astrawin::cpu::x86::StepResult::kHalted ||
        x86_iat_state.gpr[0] != 7654U ||
        x86_iat_state.gpr[4] != x86_iat_base + 256U ||
        x86_iat_state.eip != x86_iat_base + 7U) {
        return false;
    }

    CallDispatcher x64_dispatcher(true);
    std::uint64_t x64_set_error_thunk = 0U;
    std::uint64_t x64_pid_thunk = 0U;
    if (!x64_dispatcher.resolve_import(
            set_error_import, x64_set_error_thunk, error) ||
        !x64_dispatcher.resolve_import(
            get_pid_import, x64_pid_thunk, error) ||
        x64_dispatcher.thunk_count() != 2U ||
        x64_dispatcher.find_thunk(x64_set_error_thunk) != ApiId::kSetLastError) {
        return false;
    }

    constexpr std::uint64_t x64_memory_base = 0x3000U;
    astrawin::memory::GuestMemory x64_memory(256U, x64_memory_base);
    Environment x64_environment(9876U);
    constexpr std::uint64_t x64_return_address = 0x0000000140001234ULL;
    constexpr std::uint64_t x64_set_error_stack = x64_memory_base + 64U;
    constexpr std::uint32_t x64_last_error = 0xABCD1234U;

    astrawin::cpu::x86_64::CpuState x64_state{};
    x64_state.rip = x64_set_error_thunk;
    x64_state.gpr[4] = x64_set_error_stack;
    x64_state.gpr[1] = x64_last_error; // RCX carries the first Windows x64 argument.
    if (!x64_memory.write_value(x64_set_error_stack, x64_return_address) ||
        x64_dispatcher.step_x64(
            x64_state, x64_memory, x64_environment) !=
            astrawin::cpu::x86_64::StepResult::kRunning ||
        x64_state.rip != x64_return_address ||
        x64_state.gpr[4] != x64_set_error_stack + 8U ||
        x64_environment.last_error() != x64_last_error) {
        return false;
    }

    constexpr std::uint64_t x64_pid_stack = x64_memory_base + 96U;
    x64_state.rip = x64_pid_thunk;
    x64_state.gpr[4] = x64_pid_stack;
    if (!x64_memory.write_value(x64_pid_stack, x64_return_address) ||
        x64_dispatcher.step_x64(
            x64_state, x64_memory, x64_environment) !=
            astrawin::cpu::x86_64::StepResult::kRunning ||
        x64_state.rip != x64_return_address ||
        x64_state.gpr[4] != x64_pid_stack + 8U ||
        x64_state.gpr[0] != 9876U) {
        return false;
    }

    astrawin::cpu::x86_64::CpuState wrong_mode_state{};
    if (x86_dispatcher.step_x64(
            wrong_mode_state, x64_memory, x64_environment) !=
        astrawin::cpu::x86_64::StepResult::kUnsupportedInstruction) {
        return false;
    }

    // In 64-bit mode FF 15 uses RIP-relative addressing to find the IAT slot.
    constexpr std::uint64_t x64_iat_base = 0x5000U;
    constexpr std::uint64_t x64_iat_slot = x64_iat_base + 0x80U;
    astrawin::memory::GuestMemory x64_iat_memory(256U, x64_iat_base);
    std::vector<std::uint8_t> x64_iat_code(256U, 0x90U);
    x64_iat_code[0] = 0xFFU;
    x64_iat_code[1] = 0x15U; // CALL QWORD PTR [RIP + disp32]
    x64_iat_code[2] = 0x7AU; // base + 6 + 0x7A == IAT slot
    x64_iat_code[3] = 0U;
    x64_iat_code[4] = 0U;
    x64_iat_code[5] = 0U;
    x64_iat_code[6] = 0xF4U;
    if (!x64_iat_memory.write(x64_iat_base, x64_iat_code) ||
        !x64_iat_memory.write_value(x64_iat_slot, x64_pid_thunk)) {
        return false;
    }
    astrawin::cpu::x86_64::CpuState x64_iat_state{};
    x64_iat_state.rip = x64_iat_base;
    x64_iat_state.gpr[4] = x64_iat_base + 256U;
    return x64_dispatcher.run_x64(
               x64_iat_state, x64_iat_memory, x64_environment, 8U) ==
               astrawin::cpu::x86_64::StepResult::kHalted &&
           x64_iat_state.gpr[0] == 9876U &&
           x64_iat_state.gpr[4] == x64_iat_base + 256U &&
           x64_iat_state.rip == x64_iat_base + 7U;
}

bool test_guest_process_lifecycle() {
    using astrawin::process::GuestProcess;
    using astrawin::process::ProcessState;
    using astrawin::process::ThreadState;

    astrawin::pe::LoadedPeImage image{};
    image.info.architecture = astrawin::pe::PeArchitecture::kX64;
    image.info.is_64_bit = true;
    image.info.entry_point_rva = 0x1000U;
    image.info.image_base = 0x00400000U;
    image.size_of_image = 0x2000U;
    image.bytes.resize(0x2000U, 0U);
    image.bytes[0x1000U] = 0xF4U;

    std::string error;
    auto process = GuestProcess::create(77U, image, error);
    if (!process || process->process_id() != 77U ||
        process->state() != ProcessState::kCreated ||
        process->threads().size() != 1U ||
        process->threads()[0].instruction_pointer != 0x00401000U ||
        process->memory().base_address() != 0x00400000U) {
        return false;
    }

    std::uint8_t first_byte = 0;
    if (!process->memory().read_value(0x00401000U, first_byte) ||
        first_byte != 0xF4U ||
        process->memory().read_value(0x1000U, first_byte)) {
        return false;
    }
    const auto new_thread = process->create_thread(0x00401100U, error);
    if (!new_thread || *new_thread != 2U ||
        !process->set_thread_state(*new_thread, ThreadState::kRunning)) {
        return false;
    }

    process->start();
    if (process->state() != ProcessState::kRunning ||
        process->threads().front().state != ThreadState::kRunning) {
        return false;
    }

    while (process->threads().size() < GuestProcess::kMaximumThreadCount) {
        if (!process->create_thread(0x00401100U, error)) {
            return false;
        }
    }
    if (process->create_thread(0x00401200U, error)) {
        return false;
    }
    auto invalid_architecture_image = image;
    invalid_architecture_image.info.architecture =
        static_cast<astrawin::pe::PeArchitecture>(0xFF);
    std::string architecture_error;
    if (GuestProcess::create(
            80U, std::move(invalid_architecture_image), architecture_error)) {
        return false;
    }

    auto invalid_x86_image = image;
    invalid_x86_image.info.architecture = astrawin::pe::PeArchitecture::kX86;
    invalid_x86_image.info.is_64_bit = false;
    invalid_x86_image.info.image_base = 0xFFFFF000ULL;
    std::string x86_range_error;
    if (GuestProcess::create(78U, std::move(invalid_x86_image), x86_range_error)) {
        return false;
    }

    auto overflowing_x64_image = image;
    overflowing_x64_image.info.image_base = std::numeric_limits<std::uint64_t>::max() - 0x100U;
    std::string x64_range_error;
    if (GuestProcess::create(79U, std::move(overflowing_x64_image), x64_range_error)) {
        return false;
    }

    process->terminate(123U);
    if (process->state() != ProcessState::kExited ||
        process->exit_code() != 123U ||
        process->threads()[0].state != ThreadState::kExited ||
        process->create_thread(0x00401200U, error)) {
        return false;
    }
    return true;
}


bool test_guest_path_mapper() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "astrawin_path_mapper_test";
    const auto outside = fs::temp_directory_path() / "astrawin_path_mapper_outside";
    std::error_code ec;
    fs::remove_all(root, ec);
    ec.clear();
    fs::remove_all(outside, ec);
    ec.clear();
    fs::create_directories(root / "drive_c", ec);
    if (ec) {
        return false;
    }
    fs::create_directories(outside, ec);
    if (ec) {
        fs::remove_all(root, ec);
        return false;
    }

    astrawin::filesystem::GuestPathMapper mapper(root);
    fs::path resolved;
    std::string error;
    const bool mapped = mapper.resolve(
        "C:\\Program Files\\Demo\\app.exe", resolved, error);
    const auto expected = fs::weakly_canonical(
        root / "drive_c" / "Program Files" / "Demo" / "app.exe", ec);
    const bool expected_path = !ec && resolved == expected;

    fs::path rejected;
    std::string traversal_error;
    const bool traversal_rejected = !mapper.resolve(
        "C:\\..\\outside.exe", rejected, traversal_error);
    std::string stream_error;
    const bool stream_rejected = !mapper.resolve(
        "C:\\file.exe:secret", rejected, stream_error);
    std::string unc_error;
    const bool unc_rejected = !mapper.resolve(
        "\\\\server\\share\\file.exe", rejected, unc_error);
    std::string relative_error;
    const bool relative_rejected = !mapper.resolve(
        "relative\\app.exe", rejected, relative_error);
    std::string drive_relative_error;
    const bool drive_relative_rejected = !mapper.resolve(
        "C:relative\\app.exe", rejected, drive_relative_error);
    std::string drive_only_error;
    const bool drive_only_rejected = !mapper.resolve(
        "C:", rejected, drive_only_error);

    ec.clear();
    fs::create_directory_symlink(outside, root / "drive_c" / "escape", ec);
    const bool symlink_created = !ec;
    bool symlink_escape_rejected = false;
    if (symlink_created) {
        std::string symlink_error;
        symlink_escape_rejected = !mapper.resolve(
            "C:\\escape\\outside.txt", rejected, symlink_error);
    }

    fs::remove_all(root, ec);
    fs::remove_all(outside, ec);
    return mapped && expected_path && traversal_rejected &&
           stream_rejected && unc_rejected &&
           relative_rejected && drive_relative_rejected &&
           drive_only_rejected &&
           symlink_created && symlink_escape_rejected;
}


bool test_guest_sync_primitives() {
    using astrawin::sync::GuestEvent;
    using astrawin::sync::GuestTimer;

    GuestEvent manual_event(true, true);
    if (!manual_event.wait_for(std::chrono::milliseconds(1)) ||
        !manual_event.wait_for(std::chrono::milliseconds(1))) {
        return false;
    }
    manual_event.reset();
    if (manual_event.wait_for(std::chrono::milliseconds(1))) {
        return false;
    }

    GuestEvent auto_event(false, true);
    if (!auto_event.wait_for(std::chrono::milliseconds(1)) ||
        auto_event.wait_for(std::chrono::milliseconds(1))) {
        return false;
    }
    auto_event.set();
    if (!auto_event.wait_for(std::chrono::milliseconds(1))) {
        return false;
    }

    GuestTimer timer;
    timer.arm(std::chrono::milliseconds::zero());
    const bool one_shot = timer.is_armed() && timer.consume_if_due() &&
                          !timer.is_armed();
    timer.arm(std::chrono::milliseconds::zero(),
              std::chrono::milliseconds(2));
    const bool periodic_first = timer.consume_if_due();
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    const bool periodic_second = timer.consume_if_due();
    timer.cancel();
    timer.arm(std::chrono::milliseconds::max());
    const bool very_large_delay_is_safe = timer.is_armed();
    timer.cancel();
    return one_shot && periodic_first && periodic_second &&
           very_large_delay_is_safe && !timer.is_armed();
}


bool test_graphics_device_contract() {
    astrawin::graphics::HeadlessDevice device;
    if (device.initialize({0U, 480U}) ||
        !device.initialize({640U, 480U}) ||
        device.clear({0.0F, 0.0F, 0.0F, 1.0F}) ||
        !device.begin_frame() ||
        device.present() ||
        device.resize({800U, 600U})) {
        return false;
    }

    const astrawin::graphics::ClearColor color{0.1F, 0.2F, 0.3F, 1.0F};
    if (!device.clear(color) || !device.present() ||
        device.presented_frames() != 1U) {
        return false;
    }

    if (!device.resize({800U, 600U}) || !device.begin_frame() ||
        device.clear({2.0F, 0.0F, 0.0F, 1.0F}) ||
        !device.clear({0.0F, 0.0F, 0.0F, 1.0F}) || !device.present()) {
        return false;
    }
    const auto caps = device.capabilities();
    return caps.backend == astrawin::graphics::BackendKind::kHeadless &&
           !caps.hardware_accelerated &&
           device.presented_frames() == 2U;
}

}  // namespace

int main() {
    const bool pe_load_ok = test_pe_section_mapping();
    const bool pe_relocation_import_ok = test_pe_relocations_and_imports();
    const bool win32_ok = test_win32_api_environment();
    const bool win32_dispatch_ok = test_win32_call_dispatcher();
    const bool process_ok = test_guest_process_lifecycle();
    const bool path_ok = test_guest_path_mapper();
    const bool sync_ok = test_guest_sync_primitives();
    const bool graphics_ok = test_graphics_device_contract();
    const bool memory_ok = test_guest_memory();
    const bool cpu_ok = test_x64_interpreter();
    const bool x86_ok = test_x86_interpreter();
    const bool x86_stack_ok = test_x86_stack_and_call();
    const bool x64_stack_ok = test_x64_stack_and_call();
    const bool pe_ok = test_pe32_plus_inspection();

    std::cout << "Guest memory: " << (memory_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x86-64 interpreter subset: " << (cpu_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x86 interpreter subset: " << (x86_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x86 stack/CALL/RET: " << (x86_stack_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x64 stack/CALL/RET: " << (x64_stack_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "PE section mapping: " << (pe_load_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "PE relocations/imports: " << (pe_relocation_import_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "PE32/PE32+ header inspection: " << (pe_ok ? "PASS" : "FAIL") << '\n';

    std::cout << "Win32 API environment: " << (win32_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Win32 API call dispatcher: " << (win32_dispatch_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Guest process lifecycle: " << (process_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Guest path mapper: " << (path_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Guest sync primitives: " << (sync_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Graphics device contract: " << (graphics_ok ? "PASS" : "FAIL") << '\n';
    return memory_ok && cpu_ok && x86_ok && x86_stack_ok && x64_stack_ok && pe_ok && pe_load_ok && pe_relocation_import_ok && win32_ok && win32_dispatch_ok && process_ok && path_ok && sync_ok && graphics_ok ? 0 : 1;
}
