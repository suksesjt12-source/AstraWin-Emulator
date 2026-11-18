#include "astrawin/pe/pe_loader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
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

template <typename T>
bool write_le(
    std::span<std::uint8_t> bytes,
    std::size_t offset,
    T value) noexcept {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }

    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (static_cast<std::uint64_t>(value) >> (index * 8U)) & 0xFFU);
    }
    return true;
}

struct SectionRange {
    std::uint64_t begin;
    std::uint64_t end;
};

struct DataDirectory {
    std::uint32_t rva{0};
    std::uint32_t size{0};

    [[nodiscard]] bool present() const noexcept {
        return rva != 0U && size != 0U;
    }
};

bool overlaps(
    const SectionRange& left,
    const SectionRange& right) noexcept {
    return left.begin < right.end && right.begin < left.end;
}

bool read_data_directory(
    std::span<const std::uint8_t> file,
    std::size_t optional,
    std::uint16_t optional_size,
    bool is_64_bit,
    std::uint32_t directory_index,
    DataDirectory& directory,
    std::string& error) {
    const std::size_t count_offset = is_64_bit ? 108U : 92U;
    const std::size_t directories_offset = is_64_bit ? 112U : 96U;
    std::uint32_t directory_count = 0;
    if (optional_size < count_offset + sizeof(std::uint32_t) ||
        !read_le(file, optional + count_offset, directory_count)) {
        error = "Optional header does not contain NumberOfRvaAndSizes";
        return false;
    }

    directory = {};
    if (directory_index >= directory_count) {
        return true;
    }

    const auto entry_offset =
        directories_offset + static_cast<std::size_t>(directory_index) * 8U;
    if (entry_offset > optional_size ||
        8U > static_cast<std::size_t>(optional_size) - entry_offset) {
        error = "Data-directory entry extends beyond the optional header";
        return false;
    }

    if (!read_le(file, optional + entry_offset, directory.rva) ||
        !read_le(file, optional + entry_offset + 4U, directory.size)) {
        error = "Data-directory entry is truncated";
        return false;
    }

    if ((directory.rva == 0U) != (directory.size == 0U)) {
        error = "Data-directory RVA and size must both be zero or non-zero";
        return false;
    }
    return true;
}

bool read_ascii_string(
    std::span<const std::uint8_t> image,
    std::uint32_t rva,
    std::string& value) {
    if (rva >= image.size()) {
        return false;
    }

    constexpr std::size_t kMaximumStringLength = 4096U;
    const auto available = image.size() - static_cast<std::size_t>(rva);
    const auto limit = std::min(available, kMaximumStringLength);
    value.clear();
    for (std::size_t index = 0; index < limit; ++index) {
        const auto character = image[static_cast<std::size_t>(rva) + index];
        if (character == 0U) {
            return true;
        }
        value.push_back(static_cast<char>(character));
    }
    return false;
}

bool apply_signed_delta(
    std::uint32_t original,
    std::uint64_t preferred_base,
    std::uint64_t actual_base,
    std::uint32_t& relocated) noexcept {
    if (actual_base >= preferred_base) {
        const auto delta = actual_base - preferred_base;
        if (delta > std::numeric_limits<std::uint32_t>::max() - original) {
            return false;
        }
        relocated = original + static_cast<std::uint32_t>(delta);
        return true;
    }

    const auto delta = preferred_base - actual_base;
    if (delta > original) {
        return false;
    }
    relocated = original - static_cast<std::uint32_t>(delta);
    return true;
}

bool apply_signed_delta(
    std::uint64_t original,
    std::uint64_t preferred_base,
    std::uint64_t actual_base,
    std::uint64_t& relocated) noexcept {
    if (actual_base >= preferred_base) {
        const auto delta = actual_base - preferred_base;
        if (delta > std::numeric_limits<std::uint64_t>::max() - original) {
            return false;
        }
        relocated = original + delta;
        return true;
    }

    const auto delta = preferred_base - actual_base;
    if (delta > original) {
        return false;
    }
    relocated = original - delta;
    return true;
}

bool apply_relocations(
    LoadedPeImage& image,
    const DataDirectory& directory,
    std::uint64_t actual_base,
    std::string& error) {
    const auto preferred_base = image.preferred_image_base;
    const bool image_moved = actual_base != preferred_base;
    if (!directory.present()) {
        if (image_moved) {
            error = "Image requires relocation but has no base-relocation directory";
            return false;
        }
        return true;
    }

    const auto image_bytes =
        std::span<std::uint8_t>(image.bytes.data(), image.bytes.size());
    const auto directory_end =
        static_cast<std::uint64_t>(directory.rva) + directory.size;
    if (directory_end > image.bytes.size()) {
        error = "Base-relocation directory lies outside the mapped image";
        return false;
    }

    std::size_t cursor = directory.rva;
    const auto end = static_cast<std::size_t>(directory_end);
    while (cursor < end) {
        if (end - cursor < 8U) {
            error = "Base-relocation block header is truncated";
            return false;
        }

        std::uint32_t page_rva = 0;
        std::uint32_t block_size = 0;
        if (!read_le(image_bytes, cursor, page_rva) ||
            !read_le(image_bytes, cursor + 4U, block_size)) {
            error = "Base-relocation block header is invalid";
            return false;
        }
        if (block_size < 8U || (block_size & 1U) != 0U ||
            block_size > end - cursor || (page_rva & 0xFFFU) != 0U) {
            error = "Base-relocation block has an invalid size or page RVA";
            return false;
        }

        const auto entry_count = (block_size - 8U) / 2U;
        for (std::size_t index = 0; index < entry_count; ++index) {
            std::uint16_t entry = 0;
            const auto entry_offset = cursor + 8U + index * 2U;
            if (!read_le(image_bytes, entry_offset, entry)) {
                error = "Base-relocation entry is truncated";
                return false;
            }

            const auto type = static_cast<std::uint16_t>(entry >> 12U);
            const auto offset = static_cast<std::uint16_t>(entry & 0x0FFFU);
            if (type == 0U) { // IMAGE_REL_BASED_ABSOLUTE padding
                continue;
            }

            const std::uint64_t target_rva =
                static_cast<std::uint64_t>(page_rva) + offset;
            if (target_rva >= image.bytes.size()) {
                error = "Base-relocation target lies outside the mapped image";
                return false;
            }

            if (image.info.is_64_bit) {
                if (type != 10U) { // IMAGE_REL_BASED_DIR64
                    error = "Unsupported relocation type for PE32+ image";
                    return false;
                }
                std::uint64_t original = 0;
                std::uint64_t relocated = 0;
                if (!read_le(image_bytes, static_cast<std::size_t>(target_rva), original) ||
                    !apply_signed_delta(
                        original, preferred_base, actual_base, relocated) ||
                    !write_le(
                        image_bytes, static_cast<std::size_t>(target_rva), relocated)) {
                    error = "PE32+ relocation value is invalid or overflows";
                    return false;
                }
            } else {
                if (type != 3U) { // IMAGE_REL_BASED_HIGHLOW
                    error = "Unsupported relocation type for PE32 image";
                    return false;
                }
                std::uint32_t original = 0;
                std::uint32_t relocated = 0;
                if (!read_le(image_bytes, static_cast<std::size_t>(target_rva), original) ||
                    !apply_signed_delta(
                        original, preferred_base, actual_base, relocated) ||
                    !write_le(
                        image_bytes, static_cast<std::size_t>(target_rva), relocated)) {
                    error = "PE32 relocation value is invalid or overflows";
                    return false;
                }
            }
        }
        cursor += block_size;
    }

    image.relocations_applied = true;
    return true;
}

bool resolve_imports(
    LoadedPeImage& image,
    const DataDirectory& directory,
    const ImportResolver& resolver,
    std::string& error) {
    if (!directory.present()) {
        return true;
    }

    const auto image_bytes =
        std::span<std::uint8_t>(image.bytes.data(), image.bytes.size());
    const auto directory_end =
        static_cast<std::uint64_t>(directory.rva) + directory.size;
    if (directory.size < 20U || directory_end > image.bytes.size()) {
        error = "Import directory is too small or lies outside the mapped image";
        return false;
    }

    const std::size_t pointer_size = image.info.is_64_bit ? 8U : 4U;
    const std::uint64_t ordinal_flag = image.info.is_64_bit
        ? 0x8000000000000000ULL
        : 0x80000000ULL;
    const std::uint64_t ordinal_payload_mask =
        image.info.is_64_bit ? 0xFFFFULL : 0xFFFFULL;

    bool descriptor_terminator_found = false;
    std::size_t module_count = 0;
    const auto directory_limit = static_cast<std::size_t>(directory_end);
    for (std::size_t descriptor = directory.rva;
         descriptor <= directory_limit && directory_limit - descriptor >= 20U;
         descriptor += 20U) {
        std::uint32_t original_first_thunk = 0;
        std::uint32_t name_rva = 0;
        std::uint32_t first_thunk = 0;
        if (!read_le(image_bytes, descriptor, original_first_thunk) ||
            !read_le(image_bytes, descriptor + 12U, name_rva) ||
            !read_le(image_bytes, descriptor + 16U, first_thunk)) {
            error = "Import descriptor is truncated";
            return false;
        }

        if (original_first_thunk == 0U && name_rva == 0U &&
            first_thunk == 0U) {
            descriptor_terminator_found = true;
            break;
        }
        if (name_rva == 0U || first_thunk == 0U) {
            error = "Import descriptor is missing its module name or IAT";
            return false;
        }
        if (++module_count > PeImageLoader::kMaximumImportModuleCount) {
            error = "Import module count exceeds the safety limit";
            return false;
        }

        std::string module_name;
        if (!read_ascii_string(image_bytes, name_rva, module_name) ||
            module_name.empty()) {
            error = "Import module name is not a bounded null-terminated string";
            return false;
        }

        const std::uint32_t lookup_thunk =
            original_first_thunk != 0U ? original_first_thunk : first_thunk;
        bool thunk_terminator_found = false;
        const auto maximum_thunks = image.bytes.size() / pointer_size;
        for (std::size_t index = 0; index < maximum_thunks; ++index) {
            const auto lookup_rva64 =
                static_cast<std::uint64_t>(lookup_thunk) + index * pointer_size;
            const auto iat_rva64 =
                static_cast<std::uint64_t>(first_thunk) + index * pointer_size;
            if (lookup_rva64 > std::numeric_limits<std::uint32_t>::max() ||
                iat_rva64 > std::numeric_limits<std::uint32_t>::max() ||
                lookup_rva64 + pointer_size > image.bytes.size() ||
                iat_rva64 + pointer_size > image.bytes.size()) {
                error = "Import lookup or IAT thunk extends beyond the mapped image";
                return false;
            }

            const auto lookup_rva = static_cast<std::size_t>(lookup_rva64);
            const auto iat_rva = static_cast<std::size_t>(iat_rva64);
            std::uint64_t thunk_value = 0;
            if (image.info.is_64_bit) {
                if (!read_le(image_bytes, lookup_rva, thunk_value)) {
                    error = "PE32+ import thunk is truncated";
                    return false;
                }
            } else {
                std::uint32_t thunk32 = 0;
                if (!read_le(image_bytes, lookup_rva, thunk32)) {
                    error = "PE32 import thunk is truncated";
                    return false;
                }
                thunk_value = thunk32;
            }

            if (thunk_value == 0U) {
                thunk_terminator_found = true;
                break;
            }
            if (image.imports.size() >= PeImageLoader::kMaximumImportCount) {
                error = "Imported symbol count exceeds the safety limit";
                return false;
            }

            ImportedSymbol symbol{};
            symbol.module = module_name;
            symbol.iat_rva = static_cast<std::uint32_t>(iat_rva);
            symbol.by_ordinal = (thunk_value & ordinal_flag) != 0U;
            if (symbol.by_ordinal) {
                if ((thunk_value & ~(ordinal_flag | ordinal_payload_mask)) != 0U) {
                    error = "Ordinal import thunk contains unsupported reserved bits";
                    return false;
                }
                symbol.ordinal = static_cast<std::uint16_t>(
                    thunk_value & ordinal_payload_mask);
                if (symbol.ordinal == 0U) {
                    error = "Import ordinal zero is invalid";
                    return false;
                }
            } else {
                if (thunk_value > std::numeric_limits<std::uint32_t>::max()) {
                    error = "Import-by-name RVA exceeds the PE address range";
                    return false;
                }
                const auto import_name_rva =
                    static_cast<std::uint32_t>(thunk_value);
                if (import_name_rva > image.bytes.size() ||
                    2U > image.bytes.size() - import_name_rva) {
                    error = "Import-by-name hint is outside the mapped image";
                    return false;
                }
                if (!read_ascii_string(
                        image_bytes, import_name_rva + 2U, symbol.name) ||
                    symbol.name.empty()) {
                    error = "Import symbol name is not a bounded null-terminated string";
                    return false;
                }
            }

            if (resolver) {
                std::uint64_t resolved_address = 0;
                std::string resolver_error;
                if (!resolver(symbol, resolved_address, resolver_error) ||
                    resolved_address == 0U ||
                    (!image.info.is_64_bit &&
                     resolved_address > std::numeric_limits<std::uint32_t>::max())) {
                    error = resolver_error.empty()
                        ? "Import resolver did not return a valid guest address"
                        : "Import resolution failed: " + resolver_error;
                    return false;
                }

                const bool wrote_iat = image.info.is_64_bit
                    ? write_le(image_bytes, iat_rva, resolved_address)
                    : write_le(
                        image_bytes, iat_rva,
                        static_cast<std::uint32_t>(resolved_address));
                if (!wrote_iat) {
                    error = "Resolved import address could not be written to the IAT";
                    return false;
                }
                symbol.resolved_address = resolved_address;
                symbol.resolved = true;
            } else {
                image.imports_resolved = false;
            }
            image.imports.push_back(std::move(symbol));
        }

        if (!thunk_terminator_found) {
            error = "Import thunk array has no null terminator within the mapped image";
            return false;
        }
    }

    if (!descriptor_terminator_found) {
        error = "Import descriptor table has no null terminator within its directory";
        return false;
    }
    return true;
}

}  // namespace

bool PeImageLoader::load(
    std::span<const std::uint8_t> file,
    LoadedPeImage& loaded,
    std::string& error,
    const PeLoadOptions& options) {
    try {
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

        const auto actual_base =
            options.load_base == 0U ? parsed_info.image_base : options.load_base;
        if (parsed_info.is_64_bit) {
            if (static_cast<std::uint64_t>(size_of_image - 1U) >
                std::numeric_limits<std::uint64_t>::max() - actual_base) {
                error = "PE32+ image address range overflows";
                return false;
            }
        } else {
            constexpr std::uint64_t kX86AddressSpaceEnd = 0x100000000ULL;
            if (actual_base >= kX86AddressSpaceEnd ||
                size_of_image > kX86AddressSpaceEnd - actual_base) {
                error = "PE32 image does not fit in the 32-bit guest address space";
                return false;
            }
        }

        DataDirectory import_directory{};
        DataDirectory relocation_directory{};
        if (!read_data_directory(
                file, optional, optional_size, parsed_info.is_64_bit,
                1U, import_directory, error) ||
            !read_data_directory(
                file, optional, optional_size, parsed_info.is_64_bit,
                5U, relocation_directory, error)) {
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
            section.raw_pointer = raw_pointer;
            section.characteristics = characteristics;
            sections.push_back(std::move(section));
        }

        if (!entry_point_is_executable) {
            error = "Entry point is not inside an executable section";
            return false;
        }

        LoadedPeImage candidate{};
        candidate.info = parsed_info;
        candidate.preferred_image_base = parsed_info.image_base;
        candidate.info.image_base = actual_base;
        candidate.size_of_image = size_of_image;
        candidate.size_of_headers = size_of_headers;
        candidate.bytes.assign(size_of_image, 0U);
        std::copy_n(file.begin(), size_of_headers, candidate.bytes.begin());

        for (const auto& section : sections) {
            if (section.raw_size == 0U) {
                continue;
            }
            std::copy_n(
                file.begin() + static_cast<std::ptrdiff_t>(section.raw_pointer),
                section.raw_size,
                candidate.bytes.begin() +
                    static_cast<std::ptrdiff_t>(section.virtual_address));
        }

        candidate.sections = std::move(sections);
        if (!apply_relocations(
                candidate, relocation_directory, actual_base, error)) {
            return false;
        }
        if (!resolve_imports(
                candidate, import_directory, options.import_resolver, error)) {
            return false;
        }

        loaded = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::bad_alloc&) {
        error = "Insufficient memory to map the PE image";
        return false;
    }
}

}  // namespace astrawin::pe
