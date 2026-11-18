#include "astrawin/memory/guest_memory.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace astrawin::memory {

GuestMemory::GuestMemory(std::size_t size, std::uint64_t base_address)
    : base_address_(base_address) {
    if (size > std::numeric_limits<std::uint64_t>::max() - base_address_) {
        throw std::length_error("Guest memory virtual range overflows");
    }
    bytes_.assign(size, 0U);
}

GuestMemory::GuestMemory(
    std::vector<std::uint8_t> bytes,
    std::uint64_t base_address)
    : base_address_(base_address), bytes_(std::move(bytes)) {
    if (bytes_.size() >
        std::numeric_limits<std::uint64_t>::max() - base_address_) {
        throw std::length_error("Guest memory virtual range overflows");
    }
}

std::size_t GuestMemory::size() const noexcept {
    return bytes_.size();
}

std::uint64_t GuestMemory::base_address() const noexcept {
    return base_address_;
}

bool GuestMemory::read(
    std::uint64_t address,
    std::span<std::uint8_t> output) const noexcept {
    if (address < base_address_) {
        return false;
    }
    const auto relative_address = address - base_address_;
    if (relative_address > bytes_.size()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(relative_address);
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
    if (address < base_address_) {
        return false;
    }
    const auto relative_address = address - base_address_;
    if (relative_address > bytes_.size()) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(relative_address);
    if (input.size() > bytes_.size() - offset) {
        return false;
    }
    std::copy(input.begin(), input.end(),
              bytes_.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

}  // namespace astrawin::memory
