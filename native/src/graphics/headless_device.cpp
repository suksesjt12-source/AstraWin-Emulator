#include "astrawin/graphics/headless_device.h"

#include <cmath>

namespace astrawin::graphics {
namespace {

bool valid_extent(Extent2D extent) noexcept {
    return extent.width > 0U && extent.height > 0U &&
           extent.width <= 4096U && extent.height <= 4096U;
}

bool valid_color(ClearColor color) noexcept {
    const auto in_range = [](float value) {
        return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
    };
    return in_range(color.red) && in_range(color.green) &&
           in_range(color.blue) && in_range(color.alpha);
}

}  // namespace

bool HeadlessDevice::initialize(Extent2D extent) {
    if (!valid_extent(extent)) {
        return false;
    }
    extent_ = extent;
    initialized_ = true;
    frame_open_ = false;
    frame_cleared_ = false;
    return true;
}

bool HeadlessDevice::resize(Extent2D extent) {
    if (!initialized_ || frame_open_ || !valid_extent(extent)) {
        return false;
    }
    extent_ = extent;
    return true;
}

bool HeadlessDevice::begin_frame() {
    if (!initialized_ || frame_open_) {
        return false;
    }
    frame_open_ = true;
    frame_cleared_ = false;
    return true;
}

bool HeadlessDevice::clear(ClearColor color) {
    if (!frame_open_ || !valid_color(color)) {
        return false;
    }
    clear_color_ = color;
    frame_cleared_ = true;
    return true;
}

bool HeadlessDevice::present() {
    if (!frame_open_ || !frame_cleared_) {
        return false;
    }
    frame_open_ = false;
    ++presented_frames_;
    return true;
}

Capabilities HeadlessDevice::capabilities() const noexcept {
    return Capabilities{};
}

std::uint64_t HeadlessDevice::presented_frames() const noexcept {
    return presented_frames_;
}

ClearColor HeadlessDevice::last_clear_color() const noexcept {
    return clear_color_;
}

}  // namespace astrawin::graphics
