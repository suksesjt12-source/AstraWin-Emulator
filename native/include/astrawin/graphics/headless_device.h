#pragma once

#include "astrawin/graphics/graphics_device.h"

namespace astrawin::graphics {

// Validation backend only. It records frame state but does not render pixels.
class HeadlessDevice final : public GraphicsDevice {
public:
    [[nodiscard]] bool initialize(Extent2D extent) override;
    [[nodiscard]] bool resize(Extent2D extent) override;
    [[nodiscard]] bool begin_frame() override;
    [[nodiscard]] bool clear(ClearColor color) override;
    [[nodiscard]] bool present() override;
    [[nodiscard]] Capabilities capabilities() const noexcept override;

    [[nodiscard]] std::uint64_t presented_frames() const noexcept;
    [[nodiscard]] ClearColor last_clear_color() const noexcept;

private:
    Extent2D extent_{};
    ClearColor clear_color_{};
    std::uint64_t presented_frames_{0};
    bool initialized_{false};
    bool frame_open_{false};
    bool frame_cleared_{false};
};

}  // namespace astrawin::graphics
