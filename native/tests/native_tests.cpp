#include "astrawin/pe/pe_loader.h"
#include "astrawin/win32/api_registry.h"
#include "astrawin/win32/environment.h"
#include "astrawin/process/guest_process.h"
#include "astrawin/filesystem/guest_path_mapper.h"
#include "astrawin/sync/guest_event.h"
#include "astrawin/sync/guest_timer.h"
#include "astrawin/graphics/headless_device.h"
#include <thread>
#include <filesystem>
#include <cstdint>
#include <iostream>
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
    return based_memory.base_address() == 0x1000U &&
           !based_memory.write_value(0x0FFFU, value) &&
           based_memory.write_value(0x1002U, value) &&
           based_memory.read_value(0x1002U, loaded_value) &&
           loaded_value == value &&
           !based_memory.read_value(0x0FFFU, loaded_value);
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
    return Interpreter{}.step(ret_state, ret_memory) ==
               StepResult::kUnsupportedInstruction &&
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
    return Interpreter{}.step(ret_state, ret_memory) ==
               StepResult::kUnsupportedInstruction &&
           !ret_state.halted;
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
    return parsed && invalid_optional_rejected &&
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
           overlap_rejected && non_executable_rejected;
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
    auto invalid_x86_image = image;
    invalid_x86_image.info.architecture = astrawin::pe::PeArchitecture::kX86;
    invalid_x86_image.info.is_64_bit = false;
    invalid_x86_image.info.image_base = 0xFFFFF000ULL;
    std::string x86_range_error;
    if (GuestProcess::create(78U, std::move(invalid_x86_image), x86_range_error)) {
        return false;
    }

    auto overflowing_x64_image = image;
    overflowing_x64_image.info.image_base = UINT64_MAX - 0x100U;
    std::string x64_range_error;
    if (GuestProcess::create(79U, std::move(overflowing_x64_image), x64_range_error)) {
        return false;
    }

    process->terminate(123U);
    if (process->state() != ProcessState::kExited ||
        process->exit_code() != 123U ||
        process->threads()[0].state != ThreadState::kExited ||
        process->create_thread(0x1200U, error)) {
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
    const bool win32_ok = test_win32_api_environment();
    const bool process_ok = test_guest_process_lifecycle();
    const bool path_ok = test_guest_path_mapper();
    const bool sync_ok = test_guest_sync_primitives();
    const bool graphics_ok = test_graphics_device_contract();
    const bool memory_ok = test_guest_memory();
    const bool cpu_ok = test_x64_interpreter();
    const bool x86_ok = test_x86_interpreter();
    const bool pe_ok = test_pe32_plus_inspection();

    std::cout << "Guest memory: " << (memory_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x86-64 interpreter subset: " << (cpu_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "x86 interpreter subset: " << (x86_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "PE section mapping: " << (pe_load_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "PE32+ header inspection: " << (pe_ok ? "PASS" : "FAIL") << '\n';

    std::cout << "Win32 API environment: " << (win32_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Guest process lifecycle: " << (process_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Guest path mapper: " << (path_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Guest sync primitives: " << (sync_ok ? "PASS" : "FAIL") << '\n';
    std::cout << "Graphics device contract: " << (graphics_ok ? "PASS" : "FAIL") << '\n';
    return memory_ok && cpu_ok && x86_ok && pe_ok && pe_load_ok && win32_ok && process_ok && path_ok && sync_ok && graphics_ok ? 0 : 1;
}
