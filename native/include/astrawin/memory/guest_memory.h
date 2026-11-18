#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace astrawin::memory {

class GuestMemory {
public:
    explicit GuestMemory(std::size_t size, std::uint64_t base_address = 0U);
    explicit GuestMemory(
        std::vector<std::uint8_t> bytes,
        std::uint64_t base_address = 0U);

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t base_address() const noexcept;
    [[nodiscard]] bool read(std::uint64_t address, std::span<std::uint8_t> output) const noexcept;
    [[nodiscard]] bool write(std::uint64_t address, std::span<const std::uint8_t> input) noexcept;

    template <typename T>
    [[nodiscard]] bool read_value(std::uint64_t address, T& output) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        auto bytes = std::span<std::uint8_t>(
            reinterpret_cast<std::uint8_t*>(&output), sizeof(T));
        return read(address, bytes);
    }

    template <typename T>
    [[nodiscard]] bool write_value(std::uint64_t address, const T& value) noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto bytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(&value), sizeof(T));
        return write(address, bytes);
    }

private:
    std::vector<std::uint8_t> bytes_;
    std::uint64_t base_address_{0U};
};

}  // namespace astrawin::memory
