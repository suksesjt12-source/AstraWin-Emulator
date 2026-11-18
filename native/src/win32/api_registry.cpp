#include "astrawin/win32/api_registry.h"

namespace astrawin::win32 {

std::optional<ApiId> ApiRegistry::resolve(
    std::string_view library,
    std::string_view symbol) noexcept {
    if (library != "kernel32.dll" &&
        library != "kernelbase.dll" &&
        library != "KERNEL32.DLL" &&
        library != "KERNELBASE.DLL") {
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
