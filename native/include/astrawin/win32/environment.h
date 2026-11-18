#pragma once

#include <chrono>
#include <cstdint>
#include <span>

#include "astrawin/win32/api_registry.h"

namespace astrawin::win32 {

class Environment {
public:
    explicit Environment(std::uint32_t process_id = 1U) noexcept;

    // Returns false for invalid arguments. The caller receives a Win32-style
    // last-error value and is responsible for guest ABI marshalling.
    [[nodiscard]] bool invoke(
        ApiId api,
        std::span<const std::uint64_t> arguments,
        std::uint64_t& result) noexcept;

    [[nodiscard]] std::uint32_t last_error() const noexcept;
    void set_last_error(std::uint32_t value) noexcept;

private:
    std::uint32_t process_id_;
    std::uint32_t last_error_{0};
    std::chrono::steady_clock::time_point start_time_;
};

}  // namespace astrawin::win32
