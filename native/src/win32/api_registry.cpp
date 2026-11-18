#include "astrawin/win32/api_registry.h"

namespace astrawin::win32 {
namespace {

constexpr char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool equals_ascii_case_insensitive(
    std::string_view left,
    std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<ApiId> ApiRegistry::resolve(
    std::string_view library,
    std::string_view symbol) noexcept {
    if (!equals_ascii_case_insensitive(library, "kernel32.dll") &&
        !equals_ascii_case_insensitive(library, "kernelbase.dll")) {
        return std::nullopt;
    }

    if (symbol == "GetLastError") {
        return ApiId::kGetLastError;
    }
    if (symbol == "SetLastError") {
        return ApiId::kSetLastError;
    }
    if (symbol == "GetTickCount64") {
        return ApiId::kGetTickCount64;
    }
    if (symbol == "GetCurrentProcessId") {
        return ApiId::kGetCurrentProcessId;
    }
    return std::nullopt;
}

}  // namespace astrawin::win32
