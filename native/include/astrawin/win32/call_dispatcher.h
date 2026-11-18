#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "astrawin/cpu/x86/interpreter.h"
#include "astrawin/cpu/x86_64/interpreter.h"
#include "astrawin/memory/guest_memory.h"
#include "astrawin/pe/pe_loader.h"
#include "astrawin/win32/environment.h"

namespace astrawin::win32 {

// Bridges a small set of imported Win32 APIs through synthetic guest thunk
// addresses. A guest execution loop must use this dispatcher instead of the
// raw CPU interpreter so imported calls can be intercepted before fetch.
class CallDispatcher {
public:
    static constexpr std::size_t kMaximumThunkCount = 1024U;
    static constexpr std::uint64_t kX86ThunkBase = 0xFFF00000ULL;
    static constexpr std::uint64_t kX64ThunkBase = 0x00007FFF00000000ULL;
    static constexpr std::uint64_t kThunkStride = 16U;

    explicit CallDispatcher(bool is_64_bit) noexcept;

    [[nodiscard]] bool resolve_import(
        const pe::ImportedSymbol& symbol,
        std::uint64_t& address,
        std::string& error);

    [[nodiscard]] std::optional<ApiId> find_thunk(
        std::uint64_t address) const noexcept;

    [[nodiscard]] bool is_64_bit() const noexcept;
    [[nodiscard]] std::size_t thunk_count() const noexcept;

    [[nodiscard]] cpu::x86::StepResult step_x86(
        cpu::x86::CpuState& state,
        memory::GuestMemory& memory,
        Environment& environment) const noexcept;

    [[nodiscard]] cpu::x86_64::StepResult step_x64(
        cpu::x86_64::CpuState& state,
        memory::GuestMemory& memory,
        Environment& environment) const noexcept;

    [[nodiscard]] cpu::x86::StepResult run_x86(
        cpu::x86::CpuState& state,
        memory::GuestMemory& memory,
        Environment& environment,
        std::size_t max_steps) const noexcept;

    [[nodiscard]] cpu::x86_64::StepResult run_x64(
        cpu::x86_64::CpuState& state,
        memory::GuestMemory& memory,
        Environment& environment,
        std::size_t max_steps) const noexcept;

private:
    struct Entry {
        std::uint64_t address{0};
        ApiId api{ApiId::kGetLastError};
    };

    [[nodiscard]] bool dispatch_x86(
        ApiId api,
        cpu::x86::CpuState& state,
        memory::GuestMemory& memory,
        Environment& environment) const noexcept;

    [[nodiscard]] bool dispatch_x64(
        ApiId api,
        cpu::x86_64::CpuState& state,
        memory::GuestMemory& memory,
        Environment& environment) const noexcept;

    bool is_64_bit_{false};
    std::vector<Entry> entries_;
};

}  // namespace astrawin::win32
