#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "astrawin/memory/guest_memory.h"
#include "astrawin/pe/pe_loader.h"

namespace astrawin::process {

enum class ProcessState {
    kCreated,
    kRunning,
    kExited
};

enum class ThreadState {
    kReady,
    kRunning,
    kBlocked,
    kExited
};

struct GuestThread {
    std::uint32_t id{0};
    std::uint64_t instruction_pointer{0};
    ThreadState state{ThreadState::kReady};
};

class GuestProcess {
public:
    [[nodiscard]] static std::optional<GuestProcess> create(
        std::uint32_t process_id,
        const pe::LoadedPeImage& image,
        std::string& error);

    [[nodiscard]] std::optional<std::uint32_t> create_thread(
        std::uint64_t entry_point,
        std::string& error);

    [[nodiscard]] bool set_thread_state(
        std::uint32_t thread_id,
        ThreadState state) noexcept;

    void start() noexcept;
    void terminate(std::uint32_t exit_code) noexcept;

    [[nodiscard]] std::uint32_t process_id() const noexcept;
    [[nodiscard]] std::uint32_t exit_code() const noexcept;
    [[nodiscard]] ProcessState state() const noexcept;
    [[nodiscard]] const std::vector<GuestThread>& threads() const noexcept;
    [[nodiscard]] memory::GuestMemory& memory() noexcept;
    [[nodiscard]] const memory::GuestMemory& memory() const noexcept;

private:
    GuestProcess(
        std::uint32_t process_id,
        memory::GuestMemory memory,
        std::vector<GuestThread> threads) noexcept;

    std::uint32_t process_id_;
    std::uint32_t next_thread_id_{2U};
    std::uint32_t exit_code_{0};
    ProcessState state_{ProcessState::kCreated};
    memory::GuestMemory memory_;
    std::vector<GuestThread> threads_;
};

}  // namespace astrawin::process
