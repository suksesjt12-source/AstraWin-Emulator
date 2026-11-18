#include "astrawin/win32/environment.h"

#include <chrono>

namespace astrawin::win32 {

Environment::Environment(
    std::uint32_t process_id,
    std::uint32_t thread_id) noexcept
    : process_id_(process_id), thread_id_(thread_id) {}

bool Environment::invoke(
    ApiId api,
    std::span<const std::uint64_t> arguments,
    std::uint64_t& result) noexcept {
    result = 0;

    switch (api) {
        case ApiId::kGetLastError:
            if (!arguments.empty()) {
                last_error_ = 87U; // ERROR_INVALID_PARAMETER
                return false;
            }
            result = last_error_;
            return true;

        case ApiId::kSetLastError:
            if (arguments.size() != 1U) {
                last_error_ = 87U;
                return false;
            }
            last_error_ = static_cast<std::uint32_t>(arguments[0]);
            return true;

        case ApiId::kGetTickCount64: {
            if (!arguments.empty()) {
                last_error_ = 87U;
                return false;
            }
            const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            result = ticks > 0 ? static_cast<std::uint64_t>(ticks) : 0U;
            return true;
        }

        case ApiId::kGetCurrentProcessId:
            if (!arguments.empty()) {
                last_error_ = 87U;
                return false;
            }
            result = process_id_;
            return true;

        case ApiId::kGetCurrentThreadId:
            if (!arguments.empty()) {
                last_error_ = 87U;
                return false;
            }
            result = thread_id_;
            return true;

        case ApiId::kGetCurrentProcess:
            if (!arguments.empty()) {
                last_error_ = 87U;
                return false;
            }
            // Windows pseudo-handle: (HANDLE)-1; it is not a kernel object.
            result = static_cast<std::uint64_t>(-1);
            return true;

        case ApiId::kGetCurrentThread:
            if (!arguments.empty()) {
                last_error_ = 87U;
                return false;
            }
            // Windows pseudo-handle: (HANDLE)-2; it is not a kernel object.
            result = static_cast<std::uint64_t>(-2);
            return true;
    }

    last_error_ = 120U; // ERROR_CALL_NOT_IMPLEMENTED
    return false;
}

std::uint32_t Environment::last_error() const noexcept {
    return last_error_;
}

void Environment::set_last_error(std::uint32_t value) noexcept {
    last_error_ = value;
}

}  // namespace astrawin::win32
