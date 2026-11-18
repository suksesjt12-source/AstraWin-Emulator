#include "astrawin/pe/pe_parser.h"

#include <cstddef>

namespace astrawin::pe {
namespace {

template <typename T>
bool read_le(std::span<const std::uint8_t> bytes, std::size_t offset, T& value) {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }

    value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<T>(bytes[offset + index]) << (index * 8U);
    }
    return true;
}

}  // namespace

bool PeParser::inspect(
    std::span<const std::uint8_t> image,
    PeImageInfo& info,
    std::string& error) {
    std::uint16_t dos_signature = 0;
    std::uint32_t pe_offset = 0;
    if (!read_le(image, 0, dos_signature) || dos_signature != 0x5A4DU) {
        error = "Invalid DOS signature";
        return false;
    }
    if (!read_le(image, 0x3CU, pe_offset)) {
        error = "DOS header is truncated";
        return false;
    }

    const auto pe = static_cast<std::size_t>(pe_offset);
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

    const std::size_t optional = pe + 24U;
    if (optional > image.size() || optional_size > image.size() - optional) {
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

    if (machine == 0x8664U && magic == 0x20BU) {
        if (optional_size < 32U ||
            !read_le(image, optional + 24U, candidate.image_base)) {
            error = "PE32+ image base is missing";
            return false;
        }
        candidate.architecture = PeArchitecture::kX64;
        candidate.is_64_bit = true;
    } else if (machine == 0x014CU && magic == 0x010BU) {
        std::uint32_t image_base_32 = 0;
        if (optional_size < 32U ||
            !read_le(image, optional + 28U, image_base_32)) {
            error = "PE32 image base is missing";
            return false;
        }
        candidate.architecture = PeArchitecture::kX86;
        candidate.image_base = image_base_32;
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
