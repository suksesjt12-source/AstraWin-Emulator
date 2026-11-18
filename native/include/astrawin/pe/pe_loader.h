#pragma once

#include <cstdint>
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

struct LoadedPeImage {
    PeImageInfo info;
    std::uint32_t size_of_image{0};
    std::uint32_t size_of_headers{0};
    std::vector<std::uint8_t> bytes;
    std::vector<LoadedSection> sections;
};

class PeImageLoader {
public:
    static constexpr std::uint32_t kMaximumImageSize = 512U * 1024U * 1024U;

    // Maps PE headers and raw section data into a zero-initialized image buffer.
    // This does not apply base relocations or resolve imports.
    [[nodiscard]] static bool load(
        std::span<const std::uint8_t> file,
        LoadedPeImage& loaded,
        std::string& error);
};

}  // namespace astrawin::pe
