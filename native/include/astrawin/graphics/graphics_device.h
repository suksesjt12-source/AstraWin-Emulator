#pragma once

#include <cstdint>

namespace astrawin::graphics {

enum class BackendKind {
    kHeadless,
    kVulkan,
    kOpenGLES
};

enum class PixelFormat {
    kRgba8Unorm,
    kBgra8Unorm
};

struct Extent2D {
    std::uint32_t width{0};
    std::uint32_t height{0};
};

struct ClearColor {
    float red{0.0F};
    float green{0.0F};
    float blue{0.0F};
    float alpha{1.0F};
};

struct Capabilities {
    BackendKind backend{BackendKind::kHeadless};
    PixelFormat format{PixelFormat::kRgba8Unorm};
    Extent2D maximum_extent{4096U, 4096U};
    bool hardware_accelerated{false};
    bool supports_vsync{false};
};

class GraphicsDevice {
public:
    virtual ~GraphicsDevice() = default;

    [[nodiscard]] virtual bool initialize(Extent2D extent) = 0;
    [[nodiscard]] virtual bool resize(Extent2D extent) = 0;
    [[nodiscard]] virtual bool begin_frame() = 0;
    [[nodiscard]] virtual bool clear(ClearColor color) = 0;
    [[nodiscard]] virtual bool present() = 0;
    [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;
};

}  // namespace astrawin::graphics
