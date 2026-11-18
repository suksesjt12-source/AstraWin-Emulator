#include "astrawin/pe/pe_loader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

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

struct SectionRange {
    std::uint64_t begin;
    std::uint64_t end;
};

bool overlaps(
    const SectionRange& left,
    const SectionRange& right) noexcept {
    return left.begin < right.end && right.begin < left.end;
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

    const auto optional = pe + 24U;
    std::uint32_t size_of_image = 0;
    std::uint32_t size_of_headers = 0;
    const std::uint16_t minimum_optional_size =
        parsed_info.is_64_bit ? 112U : 96U;
    if (optional_size < minimum_optional_size ||
        !read_le(file, optional + 56U, size_of_image) ||
        !read_le(file, optional + 60U, size_of_headers)) {
        error = "Optional header is too small for a supported PE image";
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
    if (parsed_info.entry_point_rva >= size_of_image &&
        parsed_info.entry_point_rva != 0U) {
        error = "Entry point lies outside the mapped image";
        return false;
    }

    const std::size_t section_table = optional + optional_size;
    constexpr std::size_t kSectionHeaderSize = 40U;
    const auto table_bytes =
        static_cast<std::size_t>(section_count) * kSectionHeaderSize;
    if (section_table > file.size() ||
        table_bytes > file.size() - section_table) {
        error = "Section table is truncated";
        return false;
    }
    const auto section_table_end = section_table + table_bytes;
    if (section_table_end > size_of_headers) {
        error = "SizeOfHeaders does not include the complete section table";
        return false;
    }

    std::vector<LoadedSection> sections;
    std::vector<SectionRange> virtual_ranges;
    std::vector<SectionRange> raw_ranges;
    sections.reserve(section_count);
    virtual_ranges.reserve(section_count);
    raw_ranges.reserve(section_count);
    bool entry_point_is_executable = parsed_info.entry_point_rva == 0U;

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

        if (raw_size != 0U) {
            if (raw_pointer < size_of_headers) {
                error = "Section raw data overlaps the PE headers";
                return false;
            }
            if (raw_pointer > file.size() ||
                raw_size > file.size() - static_cast<std::size_t>(raw_pointer)) {
                error = "Section raw data extends beyond the input file";
                return false;
            }
            const SectionRange raw_range{
                raw_pointer,
                static_cast<std::uint64_t>(raw_pointer) + raw_size
            };
            for (const auto& existing : raw_ranges) {
                if (overlaps(raw_range, existing)) {
                    error = "Section raw-data ranges overlap";
                    return false;
                }
            }
            raw_ranges.push_back(raw_range);
        }

        const auto mapped_size = std::max(virtual_size, raw_size);
        if (virtual_address > size_of_image ||
            mapped_size > size_of_image - virtual_address) {
            error = "Section virtual range extends beyond SizeOfImage";
            return false;
        }

        if (mapped_size != 0U) {
            if (virtual_address < size_of_headers) {
                error = "Section virtual range overlaps the mapped headers";
                return false;
            }
            const SectionRange virtual_range{
                virtual_address,
                static_cast<std::uint64_t>(virtual_address) + mapped_size
            };
            for (const auto& existing : virtual_ranges) {
                if (overlaps(virtual_range, existing)) {
                    error = "Section virtual-address ranges overlap";
                    return false;
                }
            }
            virtual_ranges.push_back(virtual_range);

            constexpr std::uint32_t kImageScnMemExecute = 0x20000000U;
            if ((characteristics & kImageScnMemExecute) != 0U &&
                parsed_info.entry_point_rva >= virtual_address &&
                parsed_info.entry_point_rva <
                    static_cast<std::uint64_t>(virtual_address) + mapped_size) {
                entry_point_is_executable = true;
            }
        }

        LoadedSection section{};
        section.name.assign(name.data());
        section.virtual_address = virtual_address;
        section.mapped_size = mapped_size;
        section.raw_size = raw_size;
        section.characteristics = characteristics;
        sections.push_back(std::move(section));
    }

    if (!entry_point_is_executable) {
        error = "Entry point is not inside an executable section";
        return false;
    }

    LoadedPeImage candidate{};
    candidate.info = parsed_info;
    candidate.size_of_image = size_of_image;
    candidate.size_of_headers = size_of_headers;
    candidate.bytes.assign(size_of_image, 0U);
    std::copy_n(file.begin(), size_of_headers, candidate.bytes.begin());

    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        if (section.raw_size == 0U) {
            continue;
        }

        std::uint32_t raw_pointer = 0;
        const auto header = section_table + index * kSectionHeaderSize;
        if (!read_le(file, header + 20U, raw_pointer)) {
            error = "Section raw-data pointer is missing";
            return false;
        }
        std::copy_n(
            file.begin() + static_cast<std::ptrdiff_t>(raw_pointer),
            section.raw_size,
            candidate.bytes.begin() +
                static_cast<std::ptrdiff_t>(section.virtual_address));
    }

    candidate.sections = std::move(sections);
    loaded = std::move(candidate);
    error.clear();
    return true;
}

}  // namespace astrawin::pe
