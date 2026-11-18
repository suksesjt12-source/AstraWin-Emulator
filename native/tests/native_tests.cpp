#include <cstdint>
#include <iostream>
#include <string>
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
    return !memory.read_value(12, out_of_range);
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
    return result == StepResult::kHalted &&
           state.gpr[0] == 12U &&
           state.instructions_executed == 3U;
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
    return result == StepResult::kHalted &&
           state.gpr[0] == 7U &&
           state.instructions_executed == 3U;
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
    return parsed &&
           info.architecture == astrawin::pe::PeArchitecture::kX64 &&
           info.is_64_bit &&
           info.section_count == 3U &&
           info.entry_point_rva == 0x1000U &&
           info.image_base == 0x0000000140000000ULL;
}

}  // namespace

int main() {
    const bool memory_ok = test_guest_memory();
    const bool cpu_ok = test_x64_interpreter();
    const bool x86_ok = test_x86_interpreter();
    const bool pe_ok = test_pe32_plus_inspection();

    std::cout << "Guest memory: " << (memory_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x86-64 interpreter subset: " << (cpu_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x86 interpreter subset: " << (x86_ok ? "PASS" : "FAIL") << '\\n';
    std::cout << "PE32+ header inspection: " << (pe_ok ? "PASS" : "FAIL") << '\n';

    return memory_ok && cpu_ok && x86_ok && pe_ok ? 0 : 1;
}
