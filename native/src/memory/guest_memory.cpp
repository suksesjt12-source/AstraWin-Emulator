#include "astrawin/memory/guest_memory.h"

#include <algorithm>
#include <utility>

namespace astrawin::memory {

GuestMemory::GuestMemory(std::size_t size) : bytes_(size, 0) {}

GuestMemory::GuestMemory(std::vector<std::uint8_t> bytes)
    : bytes_(std::move(bytes)) {}

std::size_t GuestMemory::size() const noexcept {
    return bytes_.size();
}

bool GuestMemory::read(
    std::uint64_t address,
    std::span<std::uint8_t> output) const noexcept {
    if (address > bytes_.size()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(address);
    if (output.size() > bytes_.size() - offset) {
        return false;
    }
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                output.size(), output.begin());
    return true;
}

bool GuestMemory::write(
    std::uint64_t address,
    std::span<const std::uint8_t> input) noexcept {
    if (address > bytes_.size()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(address);
    if (input.size() > bytes_.size() - offset) {
        return false;
    }
    std::copy(input.begin(), input.end(),
              bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

}  // namespace astrawin::memory
