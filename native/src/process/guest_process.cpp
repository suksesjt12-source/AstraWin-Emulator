#include "astrawin/process/guest_process.h"

#include <span>
#include <utility>

namespace astrawin::process {

GuestProcess::GuestProcess(
    std::uint32_t process_id,
    memory::GuestMemory memory,
    std::vector<GuestThread> threads) noexcept
    : process_id_(process_id),
      memory_(std::move(memory)),
      threads_(std::move(threads)) {}

std::optional<GuestProcess> GuestProcess::create(
    std::uint32_t process_id,
    const pe::LoadedPeImage& image,
    std::string& error) {
    if (process_id == 0U) {
        error = "Process identifier must be non-zero";
        return std::nullopt;
    }
    if (image.bytes.empty() ||
        image.bytes.size() > pe::PeImageLoader::kMaximumImageSize ||
        image.size_of_image != image.bytes.size() ||
        image.info.entry_point_rva >= image.bytes.size()) {
        error = "Loaded image has no valid entry point";
        return std::nullopt;
    }

    memory::GuestMemory memory(image.bytes.size());
    const auto image_bytes = std::span<const std::uint8_t>(
        image.bytes.data(), image.bytes.size());
    if (!memory.write(0, image_bytes)) {
        error = "Unable to copy loaded PE image into guest memory";
        return std::nullopt;
    }

    std::vector<GuestThread> threads;
    threads.push_back(GuestThread{
        .id = 1U,
        .instruction_pointer = image.info.entry_point_rva,
        .state = ThreadState::kReady
    });

    error.clear();
    return GuestProcess(process_id, std::move(memory), std::move(threads));
}

std::optional<std::uint32_t> GuestProcess::create_thread(
    std::uint64_t entry_point,
    std::string& error) {
    if (state_ == ProcessState::kExited) {
        error = "Cannot create a thread in an exited process";
        return std::nullopt;
    }
    if (entry_point >= memory_.size()) {
        error = "Thread entry point is outside guest memory";
        return std::nullopt;
    }
    if (threads_.size() >= kMaximumThreadCount) {
        error = "Guest process reached the maximum thread count";
        return std::nullopt;
    }
    if (next_thread_id_ == 0U) {
        error = "Thread identifier space is exhausted";
        return std::nullopt;
    }

    const auto id = next_thread_id_++;
    threads_.push_back(GuestThread{
        .id = id,
        .instruction_pointer = entry_point,
        .state = ThreadState::kReady
    });
    error.clear();
    return id;
}

bool GuestProcess::set_thread_state(
    std::uint32_t thread_id,
    ThreadState state) noexcept {
    for (auto& thread : threads_) {
        if (thread.id == thread_id) {
            if (thread.state == ThreadState::kExited) {
                return false;
            }
            thread.state = state;
            return true;
        }
    }
    return false;
}

void GuestProcess::start() noexcept {
    if (state_ == ProcessState::kCreated) {
        state_ = ProcessState::kRunning;
    }
}

void GuestProcess::terminate(std::uint32_t exit_code) noexcept {
    if (state_ == ProcessState::kExited) {
        return;
    }
    state_ = ProcessState::kExited;
    exit_code_ = exit_code;
    for (auto& thread : threads_) {
        thread.state = ThreadState::kExited;
    }
}

std::uint32_t GuestProcess::process_id() const noexcept {
    return process_id_;
}

std::uint32_t GuestProcess::exit_code() const noexcept {
    return exit_code_;
}

ProcessState GuestProcess::state() const noexcept {
    return state_;
}

const std::vector<GuestThread>& GuestProcess::threads() const noexcept {
    return threads_;
}

memory::GuestMemory& GuestProcess::memory() noexcept {
    return memory_;
}

const memory::GuestMemory& GuestProcess::memory() const noexcept {
    return memory_;
}

}  // namespace astrawin::process
