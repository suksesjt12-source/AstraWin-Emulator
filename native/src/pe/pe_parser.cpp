#include "astrawin/pe/pe_parser.h"

#include <cstddef>
#include <cstdint>

namespace astrawin::pe {
namespace {

template <typename T>
bool read_le(
    std::span<const std::uint8_t> bytes,
    std::size_t offset,
    T& value) noexcept {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }

    std::uint64_t accumulator = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        accumulator |= static_cast<std::uint64_t>(bytes[offset + index])
                       << (index * 8U);
    }
    value = static_cast<T>(accumulator);
    return true;
}

}  // namespace

bool PeParser::inspect(
    std::span<const std::uint8_t> image,
    PeImageInfo& info,
    std::string& error) {
    constexpr std::size_t kDosHeaderMinimumSize = 0x40U;
    constexpr std::size_t kCoffHeaderSize = 24U;
    constexpr std::uint16_t kMaximumSectionCount = 96U;
    constexpr std::uint16_t kMinimumOptionalHeaderSize = 32U;

    std::uint16_t dos_signature = 0;
    std::uint32_t pe_offset_raw = 0;
    if (!read_le(image, 0, dos_signature) || dos_signature != 0x5A4DU) {
        error = "Invalid DOS signature";
        return false;
    }
    if (image.size() < kDosHeaderMinimumSize ||
        !read_le(image, 0x3CU, pe_offset_raw)) {
        error = "DOS header is truncated";
        return false;
    }

    const auto pe = static_cast<std::size_t>(pe_offset_raw);
    if (pe < kDosHeaderMinimumSize ||
        pe > image.size() ||
        kCoffHeaderSize > image.size() - pe) {
        error = "PE/COFF header offset is outside the input file";
        return false;
    }

    std::uint32_t signature = 0;
    std::uint16_t machine = 0;
    std::uint16_t sections = 0;
    std::uint16_t optional_size = 0;
    if (!read_le(image, pe, signature) || signature != 0x00004550U) {
        error = "Invalid or missing PE signature";
        return false;
    }
    if (!read_le(image, pe + 4U, machine) ||
        !read_le(image, pe + 6U, sections) ||
        !read_le(image, pe + 20U, optional_size)) {
        error = "COFF header is truncated";
        return false;
    }
    if (sections > kMaximumSectionCount) {
        error = "Section count exceeds the supported safety limit";
        return false;
    }
    if (optional_size < kMinimumOptionalHeaderSize) {
        error = "Optional header is too small for the supported PE formats";
        return false;
    }

    const std::size_t optional = pe + kCoffHeaderSize;
    if (optional > image.size() ||
        optional_size > image.size() - optional) {
        error = "Optional header is truncated";
        return false;
    }

    std::uint16_t magic = 0;
    std::uint32_t entry_point = 0;
    if (!read_le(image, optional, magic) ||
        !read_le(image, optional + 16U, entry_point)) {
        error = "Optional header fields are missing";
        return false;
    }

    PeImageInfo candidate{};
    candidate.section_count = sections;
    candidate.entry_point_rva = entry_point;

    if (machine == 0x8664U && magic == 0x020BU) {
        std::uint64_t image_base = 0;
        if (!read_le(image, optional + 24U, image_base)) {
            error = "PE32+ image base is missing";
            return false;
        }
        candidate.architecture = PeArchitecture::kX64;
        candidate.image_base = image_base;
        candidate.is_64_bit = true;
    } else if (machine == 0x014CU && magic == 0x010BU) {
        std::uint32_t image_base = 0;
        if (!read_le(image, optional + 28U, image_base)) {
            error = "PE32 image base is missing";
            return false;
        }
        candidate.architecture = PeArchitecture::kX86;
        candidate.image_base = image_base;
        candidate.is_64_bit = false;
    } else {
        error = "Unsupported machine or PE optional-header format";
        return false;
    }

    info = candidate;
    error.clear();
    return true;
}

}  // namespace astrawin::pe
