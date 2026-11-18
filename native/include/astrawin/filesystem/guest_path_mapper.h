#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace astrawin::filesystem {

class GuestPathMapper {
public:
    explicit GuestPathMapper(std::filesystem::path sandbox_root);

    // Maps drive-qualified Windows paths to drive_c/ under the sandbox.
    // Traversal, UNC paths, alternate data streams and symlink escapes are rejected.
    [[nodiscard]] bool resolve(
        std::string_view guest_path,
        std::filesystem::path& host_path,
        std::string& error) const;

private:
    std::filesystem::path root_;
};

}  // namespace astrawin::filesystem
