#include "astrawin/win32/call_dispatcher.h"

#include <array>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace astrawin::win32 {

CallDispatcher::CallDispatcher(bool is_64_bit) noexcept
    : is_64_bit_(is_64_bit) {}

bool CallDispatcher::resolve_import(
    const pe::ImportedSymbol& symbol,
    std::uint64_t& address,
    std::string& error) {
    address = 0U;
    if (symbol.by_ordinal || symbol.name.empty()) {
        error = "The initial Win32 API registry supports named imports only";
        return false;
    }

    const auto api = ApiRegistry::resolve(symbol.module, symbol.name);
    if (!api) {
        error = "Imported Win32 API is not implemented: " +
                symbol.module + "!" + symbol.name;
        return false;
    }

    for (const auto& entry : entries_) {
        if (entry.api == *api) {
            address = entry.address;
            error.clear();
            return true;
        }
    }

    if (entries_.size() >= kMaximumThunkCount) {
        error = "Win32 API thunk table reached its capacity";
        return false;
    }

    const auto base = is_64_bit_ ? kX64ThunkBase : kX86ThunkBase;
    const auto offset =
        static_cast<std::uint64_t>(entries_.size()) * kThunkStride;
    if (offset > std::numeric_limits<std::uint64_t>::max() - base) {
        error = "Win32 API thunk address overflow";
        return false;
    }

    const auto candidate_address = base + offset;
    try {
        entries_.push_back(Entry{candidate_address, *api});
    } catch (const std::bad_alloc&) {
        error = "Insufficient memory to allocate a Win32 API thunk";
        return false;
    }

    address = candidate_address;
    error.clear();
    return true;
}

std::optional<ApiId> CallDispatcher::find_thunk(
    std::uint64_t address) const noexcept {
    for (const auto& entry : entries_) {
        if (entry.address == address) {
            return entry.api;
        }
    }
    return std::nullopt;
}

bool CallDispatcher::is_64_bit() const noexcept {
    return is_64_bit_;
}

std::size_t CallDispatcher::thunk_count() const noexcept {
    return entries_.size();
}

bool CallDispatcher::dispatch_x86(
    ApiId api,
    cpu::x86::CpuState& state,
    memory::GuestMemory& memory,
    Environment& environment) const noexcept {
    const auto stack_pointer = state.gpr[4];
    constexpr std::uint32_t kReturnAddressSize = 4U;
    if (stack_pointer >
        std::numeric_limits<std::uint32_t>::max() - kReturnAddressSize) {
        return false;
    }

    std::uint32_t return_address = 0U;
    if (!memory.read_value(stack_pointer, return_address)) {
        return false;
    }

    std::array<std::uint64_t, 1> arguments{};
    std::span<const std::uint64_t> argument_span{};
    std::uint32_t callee_cleanup = 0U;
    if (api == ApiId::kSetLastError ||
        api == ApiId::kGetSystemTimeAsFileTime) {
        const auto argument_address =
            stack_pointer + kReturnAddressSize;
        if (argument_address >
            std::numeric_limits<std::uint32_t>::max() - 4U) {
            return false;
        }
        std::uint32_t argument = 0U;
        if (!memory.read_value(argument_address, argument)) {
            return false;
        }
        arguments[0] = argument;
        argument_span = std::span<const std::uint64_t>(arguments.data(), 1U);
        callee_cleanup = 4U; // WINAPI / stdcall callee cleanup on x86.
    } else if (api != ApiId::kGetLastError &&
               api != ApiId::kGetTickCount64 &&
               api != ApiId::kGetTickCount &&
               api != ApiId::kGetCurrentProcessId &&
               api != ApiId::kGetCurrentThreadId &&
               api != ApiId::kGetCurrentProcess &&
               api != ApiId::kGetCurrentThread &&
               api != ApiId::kGetSystemTimeAsFileTime) {
        return false;
    }

    std::uint64_t result = 0U;
    if (!environment.invoke(api, argument_span, result)) {
        return false;
    }
    if (api == ApiId::kGetSystemTimeAsFileTime) {
        if (!memory.write_value(arguments[0], result)) {
            return false;
        }
        result = 0U; // The Win32 function returns void; FILETIME is an output parameter.
    }

    const auto stack_increment =
        static_cast<std::uint32_t>(kReturnAddressSize + callee_cleanup);
    if (stack_increment >
        std::numeric_limits<std::uint32_t>::max() - stack_pointer) {
        return false;
    }

    state.eip = return_address;
    state.gpr[4] = stack_pointer + stack_increment;
    state.gpr[0] = static_cast<std::uint32_t>(result);
    if (api == ApiId::kGetTickCount64) {
        state.gpr[2] = static_cast<std::uint32_t>(result >> 32U);
    }
    return true;
}

bool CallDispatcher::dispatch_x64(
    ApiId api,
    cpu::x86_64::CpuState& state,
    memory::GuestMemory& memory,
    Environment& environment) const noexcept {
    const auto stack_pointer = state.gpr[4];
    constexpr std::uint64_t kReturnAddressSize = 8U;
    if (stack_pointer >
        std::numeric_limits<std::uint64_t>::max() - kReturnAddressSize) {
        return false;
    }

    std::uint64_t return_address = 0U;
    if (!memory.read_value(stack_pointer, return_address)) {
        return false;
    }

    std::array<std::uint64_t, 1> arguments{};
    std::span<const std::uint64_t> argument_span{};
    if (api == ApiId::kSetLastError ||
        api == ApiId::kGetSystemTimeAsFileTime) {
        // Windows x64 passes the first integer or pointer argument in RCX.
        arguments[0] = api == ApiId::kSetLastError
            ? static_cast<std::uint32_t>(state.gpr[1])
            : state.gpr[1];
        argument_span = std::span<const std::uint64_t>(arguments.data(), 1U);
    } else if (api != ApiId::kGetLastError &&
               api != ApiId::kGetTickCount64 &&
               api != ApiId::kGetTickCount &&
               api != ApiId::kGetCurrentProcessId &&
               api != ApiId::kGetCurrentThreadId &&
               api != ApiId::kGetCurrentProcess &&
               api != ApiId::kGetCurrentThread &&
               api != ApiId::kGetSystemTimeAsFileTime) {
        return false;
    }

    std::uint64_t result = 0U;
    if (!environment.invoke(api, argument_span, result)) {
        return false;
    }
    if (api == ApiId::kGetSystemTimeAsFileTime) {
        if (!memory.write_value(arguments[0], result)) {
            return false;
        }
        result = 0U; // The Win32 function returns void; FILETIME is an output parameter.
    }

    state.rip = return_address;
    state.gpr[4] = stack_pointer + kReturnAddressSize;
    state.gpr[0] = result;
    return true;
}

cpu::x86::StepResult CallDispatcher::step_x86(
    cpu::x86::CpuState& state,
    memory::GuestMemory& memory,
    Environment& environment) const noexcept {
    if (is_64_bit_) {
        return cpu::x86::StepResult::kUnsupportedInstruction;
    }

    const auto api = find_thunk(state.eip);
    if (!api) {
        return cpu::x86::Interpreter{}.step(state, memory);
    }
    if (!dispatch_x86(*api, state, memory, environment)) {
        return cpu::x86::StepResult::kMemoryFault;
    }
    ++state.instructions_executed;
    return cpu::x86::StepResult::kRunning;
}

cpu::x86_64::StepResult CallDispatcher::step_x64(
    cpu::x86_64::CpuState& state,
    memory::GuestMemory& memory,
    Environment& environment) const noexcept {
    if (!is_64_bit_) {
        return cpu::x86_64::StepResult::kUnsupportedInstruction;
    }

    const auto api = find_thunk(state.rip);
    if (!api) {
        return cpu::x86_64::Interpreter{}.step(state, memory);
    }
    if (!dispatch_x64(*api, state, memory, environment)) {
        return cpu::x86_64::StepResult::kMemoryFault;
    }
    ++state.instructions_executed;
    return cpu::x86_64::StepResult::kRunning;
}

cpu::x86::StepResult CallDispatcher::run_x86(
    cpu::x86::CpuState& state,
    memory::GuestMemory& memory,
    Environment& environment,
    std::size_t max_steps) const noexcept {
    for (std::size_t index = 0; index < max_steps; ++index) {
        const auto result = step_x86(state, memory, environment);
        if (result != cpu::x86::StepResult::kRunning) {
            return result;
        }
    }
    return cpu::x86::StepResult::kStepLimitExceeded;
}

cpu::x86_64::StepResult CallDispatcher::run_x64(
    cpu::x86_64::CpuState& state,
    memory::GuestMemory& memory,
    Environment& environment,
    std::size_t max_steps) const noexcept {
    for (std::size_t index = 0; index < max_steps; ++index) {
        const auto result = step_x64(state, memory, environment);
        if (result != cpu::x86_64::StepResult::kRunning) {
            return result;
        }
    }
    return cpu::x86_64::StepResult::kStepLimitExceeded;
}

}  // namespace astrawin::win32
