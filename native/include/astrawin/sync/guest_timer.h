#pragma once

#include <chrono>
#include <mutex>

namespace astrawin::sync {

class GuestTimer {
public:
    using Clock = std::chrono::steady_clock;

    void arm(std::chrono::milliseconds delay,
             std::chrono::milliseconds period = std::chrono::milliseconds::zero());
    void cancel();
    [[nodiscard]] bool is_armed() const;
    [[nodiscard]] bool consume_if_due();

private:
    mutable std::mutex mutex_;
    Clock::time_point deadline_{};
    std::chrono::milliseconds period_{0};
    bool armed_{false};
};

}  // namespace astrawin::sync
