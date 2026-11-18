#include "astrawin/sync/guest_event.h"

namespace astrawin::sync {

GuestEvent::GuestEvent(bool manual_reset, bool initially_signaled) noexcept
    : manual_reset_(manual_reset), signaled_(initially_signaled) {}

void GuestEvent::set() {
    {
        std::lock_guard lock(mutex_);
        signaled_ = true;
    }
    if (manual_reset_) {
        condition_.notify_all();
    } else {
        condition_.notify_one();
    }
}

void GuestEvent::reset() {
    std::lock_guard lock(mutex_);
    signaled_ = false;
}

bool GuestEvent::wait_for(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    if (!condition_.wait_for(lock, timeout, [this] { return signaled_; })) {
        return false;
    }
    if (!manual_reset_) {
        signaled_ = false;
    }
    return true;
}

}  // namespace astrawin::sync
