#include "astrawin/sync/guest_timer.h"

namespace astrawin::sync {

void GuestTimer::arm(
    std::chrono::milliseconds delay,
    std::chrono::milliseconds period) {
    constexpr auto kMaximumDuration = std::chrono::hours(24 * 365);
    const auto maximum_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(kMaximumDuration);

    if (delay < std::chrono::milliseconds::zero()) {
        delay = std::chrono::milliseconds::zero();
    } else if (delay > maximum_ms) {
        delay = maximum_ms;
    }
    if (period < std::chrono::milliseconds::zero()) {
        period = std::chrono::milliseconds::zero();
    } else if (period > maximum_ms) {
        period = maximum_ms;
    }

    std::lock_guard lock(mutex_);
    deadline_ = Clock::now() + delay;
    period_ = period;
    armed_ = true;
}

void GuestTimer::cancel() {
    std::lock_guard lock(mutex_);
    armed_ = false;
}

bool GuestTimer::is_armed() const {
    std::lock_guard lock(mutex_);
    return armed_;
}

bool GuestTimer::consume_if_due() {
    std::lock_guard lock(mutex_);
    if (!armed_ || Clock::now() < deadline_) {
        return false;
    }

    if (period_ == std::chrono::milliseconds::zero()) {
        armed_ = false;
    } else {
        // Avoid a burst of catch-up expirations after the host was paused.
        deadline_ = Clock::now() + period_;
    }
    return true;
}

}  // namespace astrawin::sync
