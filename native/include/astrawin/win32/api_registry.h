#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace astrawin::win32 {

enum class ApiId {
    kGetLastError,
    kSetLastError,
    kGetTickCount64,
    kGetTickCount,
    kGetSystemTimeAsFileTime,
    kGetCurrentProcessId,
    kGetCurrentThreadId,
    kGetCurrentProcess,
    kGetCurrentThread
};

class ApiRegistry {
public:
    // Resolves a deliberately small kernel32/kernelbase API subset.
    [[nodiscard]] static std::optional<ApiId> resolve(
        std::string_view library,
        std::string_view symbol) noexcept;
};

}  // namespace astrawin::win32
