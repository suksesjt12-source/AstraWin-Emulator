#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "astrawin/pe/pe_parser.h"

namespace astrawin::pe {

struct LoadedSection {
    std::string name;
    std::uint32_t virtual_address{0};
    std::uint32_t mapped_size{0};
    std::uint32_t raw_size{0};
    std::uint32_t raw_pointer{0};
    std::uint32_t characteristics{0};
};

struct ImportedSymbol {
    std::string module;
    std::string name;
    std::uint16_t ordinal{0};
    bool by_ordinal{false};
    std::uint32_t iat_rva{0};
    std::uint64_t resolved_address{0};
    bool resolved{false};
};

using ImportResolver = std::function<bool(
    const ImportedSymbol& symbol,
    std::uint64_t& address,
    std::string& error)>;

struct PeLoadOptions {
    // Zero selects the preferred ImageBase stored in the PE optional header.
    std::uint64_t load_base{0};
    // When omitted, imports are enumerated but IAT entries remain unresolved.
    ImportResolver import_resolver{};
};

struct LoadedPeImage {
    PeImageInfo info;
    std::uint64_t preferred_image_base{0};
    std::uint32_t size_of_image{0};
    std::uint32_t size_of_headers{0};
    std::vector<std::uint8_t> bytes;
    std::vector<LoadedSection> sections;
    std::vector<ImportedSymbol> imports;
    bool relocations_applied{false};
    bool imports_resolved{true};
};

class PeImageLoader {
public:
    static constexpr std::uint32_t kMaximumImageSize = 512U * 1024U * 1024U;
    static constexpr std::size_t kMaximumImportCount = 65536U;
    static constexpr std::size_t kMaximumImportModuleCount = 1024U;

    // Maps headers/sections and applies supported PE base relocations.
    // Import descriptors are validated and enumerated. A supplied resolver
    // receives each symbol and can populate its IAT entry.
    [[nodiscard]] static bool load(
        std::span<const std::uint8_t> file,
        LoadedPeImage& loaded,
        std::string& error,
        const PeLoadOptions& options = {});
};

}  // namespace astrawin::pe
