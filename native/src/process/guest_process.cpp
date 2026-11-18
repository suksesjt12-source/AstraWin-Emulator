#include "astrawin/process/guest_process.h"

#include <limits>
#include <new>
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
    pe::LoadedPeImage image,
    std::string& error) {
    try {
        if (process_id == 0U) {
            error = "Process identifier must be non-zero";
            return std::nullopt;
        }
        if ((image.info.architecture != pe::PeArchitecture::kX86 &&
             image.info.architecture != pe::PeArchitecture::kX64) ||
            image.bytes.empty() ||
            image.bytes.size() > pe::PeImageLoader::kMaximumImageSize ||
            image.size_of_image != image.bytes.size() ||
            image.info.entry_point_rva >= image.bytes.size() ||
            image.info.is_64_bit !=
                (image.info.architecture == pe::PeArchitecture::kX64)) {
            error = "Loaded image metadata or entry point is invalid";
            return std::nullopt;
        }
    
        const auto image_size = static_cast<std::uint64_t>(image.bytes.size());
        const auto image_base = image.info.image_base;
        if (image_base > std::numeric_limits<std::uint64_t>::max() - image_size) {
            error = "PE image virtual address range overflows";
            return std::nullopt;
        }
        const auto image_end = image_base + image_size;
        if (!image.info.is_64_bit && image_end > (1ULL << 32U)) {
            error = "32-bit PE image exceeds the 32-bit guest address space";
            return std::nullopt;
        }
    
        memory::GuestMemory memory(std::move(image.bytes), image_base);
    
        std::vector<GuestThread> threads;
        threads.push_back(GuestThread{
            .id = 1U,
            .instruction_pointer = image_base + image.info.entry_point_rva,
            .state = ThreadState::kReady
        });
    
        error.clear();
        return GuestProcess(process_id, std::move(memory), std::move(threads));
    } catch (const std::bad_alloc&) {
        error = "Insufficient memory to create the guest process";
        return std::nullopt;
    }
}

std::optional<std::uint32_t> GuestProcess::create_thread(
    std::uint64_t entry_point,
    std::string& error) {
    if (state_ == ProcessState::kExited) {
        error = "Cannot create a thread in an exited process";
        return std::nullopt;
    }
    const auto memory_base = memory_.base_address();
    if (entry_point < memory_base ||
        entry_point - memory_base >= memory_.size()) {
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

    const auto id = next_thread_id_;
    try {
        threads_.push_back(GuestThread{
            .id = id,
            .instruction_pointer = entry_point,
            .state = ThreadState::kReady
        });
    } catch (const std::bad_alloc&) {
        error = "Insufficient memory to create the guest thread";
        return std::nullopt;
    }
    ++next_thread_id_;
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
        if (!threads_.empty() && threads_.front().state == ThreadState::kReady) {
            threads_.front().state = ThreadState::kRunning;
        }
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
