#include "astrawin/pe/pe_loader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

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

    value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        value |= static_cast<T>(bytes[offset + index]) << (index * 8U);
    }
    return true;
}

}  // namespace

bool PeImageLoader::load(
    std::span<const std::uint8_t> file,
    LoadedPeImage& loaded,
    std::string& error) {
    PeImageInfo parsed_info{};
    if (!PeParser::inspect(file, parsed_info, error)) {
        return false;
    }

    std::uint32_t pe_offset_raw = 0;
    if (!read_le(file, 0x3CU, pe_offset_raw)) {
        error = "DOS header is truncated";
        return false;
    }

    const auto pe = static_cast<std::size_t>(pe_offset_raw);
    std::uint16_t section_count = 0;
    std::uint16_t optional_size = 0;
    if (!read_le(file, pe + 6U, section_count) ||
        !read_le(file, pe + 20U, optional_size)) {
        error = "COFF header is truncated";
        return false;
    }
    if (section_count > 96U) {
        error = "Section count exceeds the loader safety limit";
        return false;
    }

    const auto optional = pe + 24U;
    std::uint32_t size_of_image = 0;
    std::uint32_t size_of_headers = 0;
    if (optional_size < 64U ||
        !read_le(file, optional + 56U, size_of_image) ||
        !read_le(file, optional + 60U, size_of_headers)) {
        error = "Optional header does not contain image-size fields";
        return false;
    }

    if (size_of_image == 0U || size_of_image > kMaximumImageSize) {
        error = "Image size is zero or exceeds the 512 MiB safety limit";
        return false;
    }
    if (size_of_headers == 0U ||
        size_of_headers > size_of_image ||
        size_of_headers > file.size()) {
        error = "Header size is outside the file or mapped image";
        return false;
    }
    if (parsed_info.entry_point_rva >= size_of_image) {
        error = "Entry point lies outside the mapped image";
        return false;
    }

    const std::size_t section_table = optional + optional_size;
    constexpr std::size_t kSectionHeaderSize = 40U;
    const auto table_bytes = static_cast<std::size_t>(section_count) * kSectionHeaderSize;
    if (section_table > file.size() ||
        table_bytes > file.size() - section_table) {
        error = "Section table is truncated";
        return false;
    }

    LoadedPeImage candidate{};
    candidate.info = parsed_info;
    candidate.size_of_image = size_of_image;
    candidate.size_of_headers = size_of_headers;
    candidate.bytes.assign(size_of_image, 0U);
    std::copy_n(file.begin(), size_of_headers, candidate.bytes.begin());

    for (std::size_t index = 0; index < section_count; ++index) {
        const auto header = section_table + index * kSectionHeaderSize;
        std::array<char, 9> name{};
        for (std::size_t name_index = 0; name_index < 8U; ++name_index) {
            name[name_index] = static_cast<char>(file[header + name_index]);
        }

        std::uint32_t virtual_size = 0;
        std::uint32_t virtual_address = 0;
        std::uint32_t raw_size = 0;
        std::uint32_t raw_pointer = 0;
        std::uint32_t characteristics = 0;
        if (!read_le(file, header + 8U, virtual_size) ||
            !read_le(file, header + 12U, virtual_address) ||
            !read_le(file, header + 16U, raw_size) ||
            !read_le(file, header + 20U, raw_pointer) ||
            !read_le(file, header + 36U, characteristics)) {
            error = "Section header is truncated";
            return false;
        }

        if (raw_pointer > file.size() ||
            raw_size > file.size() - static_cast<std::size_t>(raw_pointer)) {
            error = "Section raw data extends beyond the input file";
            return false;
        }

        const auto mapped_size = std::max(virtual_size, raw_size);
        if (virtual_address > size_of_image ||
            mapped_size > size_of_image - virtual_address) {
            error = "Section virtual range extends beyond SizeOfImage";
            return false;
        }
        if (mapped_size != 0U && virtual_address < size_of_headers) {
            error = "Section virtual range overlaps the mapped headers";
            return false;
        }

        if (raw_size != 0U) {
            std::copy_n(
                file.begin() + static_cast<std::ptrdiff_t>(raw_pointer),
                raw_size,
                candidate.bytes.begin() + static_cast<std::ptrdiff_t>(virtual_address));
        }

        LoadedSection section{};
        section.name.assign(name.data());
        section.virtual_address = virtual_address;
        section.mapped_size = mapped_size;
        section.raw_size = raw_size;
        section.characteristics = characteristics;
        candidate.sections.push_back(std::move(section));
    }

    loaded = std::move(candidate);
    error.clear();
    return true;
}

}  // namespace astrawin::pe
