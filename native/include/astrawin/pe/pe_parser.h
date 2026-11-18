#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace astrawin::pe {

enum class PeArchitecture {
    kX86,
    kX64
};

struct PeImageInfo {
    PeArchitecture architecture{PeArchitecture::kX86};
    std::uint16_t section_count{0};
    std::uint32_t entry_point_rva{0};
    std::uint64_t image_base{0};
    bool is_64_bit{false};
};

class PeParser {
public:
    // Inspects DOS, COFF and optional headers. This is not a PE loader.
    [[nodiscard]] static bool inspect(
        std::span<const std::uint8_t> image,
        PeImageInfo& info,
        std::string& error);
};

}  // namespace astrawin::pe
