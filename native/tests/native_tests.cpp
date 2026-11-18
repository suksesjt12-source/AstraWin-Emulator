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

    put32(section + 20U, 0x210U);
    astrawin::pe::LoadedPeImage rejected{};
    std::string rejected_error;
    const bool malformed_rejected =
        !astrawin::pe::PeImageLoader::load(file, rejected, rejected_error);
    return mapped && malformed_rejected;
}


bool test_win32_api_environment() {
    using astrawin::win32::ApiRegistry;
    using astrawin::win32::Environment;

    const auto set_last_error = ApiRegistry::resolve("KeRnEl32.DlL", "SetLastError");
    const auto get_last_error = ApiRegistry::resolve("kernelbase.dll", "GetLastError");
    const auto get_pid = ApiRegistry::resolve("kernel32.dll", "GetCurrentProcessId");
    const auto unknown = ApiRegistry::resolve("user32.dll", "MessageBoxW");
    if (!set_last_error || !get_last_error || !get_pid || unknown) {
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
    image.bytes.resize(0x2000U, 0U);
    image.bytes[0x1000U] = 0xF4U;

    std::string error;
    auto process = GuestProcess::create(77U, image, error);
    if (!process || process->process_id() != 77U ||
        process->state() != ProcessState::kCreated ||
        process->threads().size() != 1U ||
        process->threads()[0].instruction_pointer != 0x1000U) {
        return false;
    }

    std::uint8_t first_byte = 0;
    if (!process->memory().read_value(0x1000U, first_byte) || first_byte != 0xF4U) {
        return false;
    }
    const auto new_thread = process->create_thread(0x1100U, error);
    if (!new_thread || *new_thread != 2U ||
        !process->set_thread_state(*new_thread, ThreadState::kRunning)) {
        return false;
    }

    process->start();
    if (process->state() != ProcessState::kRunning) {
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
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
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

    fs::remove_all(root, ec);
    return mapped && expected_path && traversal_rejected &&
           stream_rejected && unc_rejected;
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
    return one_shot && periodic_first && periodic_second && !timer.is_armed();
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
