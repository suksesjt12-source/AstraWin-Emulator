#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace astrawin::filesystem {

class GuestPathMapper {
public:
    explicit GuestPathMapper(std::filesystem::path sandbox_root);

    // Maps drive-absolute Windows paths to drive_c/ under the sandbox.
    // Relative, drive-relative, traversal, UNC, alternate-stream and symlink escapes are rejected.
    [[nodiscard]] bool resolve(
        std::string_view guest_path,
        std::filesystem::path& host_path,
        std::string& error) const;

private:
    std::filesystem::path root_;
};

}  // namespace astrawin::filesystem
