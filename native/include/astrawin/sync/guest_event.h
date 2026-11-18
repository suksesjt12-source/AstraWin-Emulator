#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace astrawin::sync {

class GuestEvent {
public:
    explicit GuestEvent(bool manual_reset = false, bool initially_signaled = false) noexcept;

    void set();
    void reset();
    [[nodiscard]] bool wait_for(std::chrono::milliseconds timeout);

private:
    const bool manual_reset_;
    bool signaled_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

}  // namespace astrawin::sync
