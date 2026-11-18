#include "astrawin/filesystem/guest_path_mapper.h"

#include <cctype>
#include <system_error>
#include <utility>

namespace astrawin::filesystem {
namespace {

bool is_drive_letter(char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool path_is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) {
            return false;
        }
    }
    return true;
}

}  // namespace

GuestPathMapper::GuestPathMapper(std::filesystem::path sandbox_root)
    : root_(std::move(sandbox_root)) {}

bool GuestPathMapper::resolve(
    std::string_view guest_path,
    std::filesystem::path& host_path,
    std::string& error) const {
    if (guest_path.empty() || guest_path.size() > 32767U) {
        error = "Guest path is empty or exceeds the supported length";
        return false;
    }

    std::string normalized(guest_path);
    for (auto& character : normalized) {
        if (character == '\\') {
            character = '/';
        }
        if (character == '\0') {
            error = "Guest path contains a null character";
            return false;
        }
    }

    if (normalized.starts_with("//")) {
        error = "UNC paths are not supported by the sandbox mapper";
        return false;
    }

    if (normalized.size() < 2U ||
        !is_drive_letter(normalized[0]) ||
        normalized[1] != ':') {
        error = "Guest path must include an explicit Windows drive letter";
        return false;
    }

    std::string drive(1U, static_cast<char>(
        std::tolower(static_cast<unsigned char>(normalized[0]))));
    std::size_t path_start = 2U;
    if (path_start < normalized.size()) {
        if (normalized[path_start] != '/') {
            error = "Drive-relative paths are not supported";
            return false;
        }
        ++path_start;
    }

    std::filesystem::path relative = std::filesystem::path("drive_" + drive);
    std::size_t component_start = path_start;
    while (component_start <= normalized.size()) {
        const auto separator = normalized.find('/', component_start);
        const auto component_end = separator == std::string::npos
            ? normalized.size()
            : separator;
        const auto component = normalized.substr(
            component_start, component_end - component_start);

        if (!component.empty() && component != ".") {
            if (component == ".." || component.find(':') != std::string::npos) {
                error = "Guest path contains a traversal or alternate-stream component";
                return false;
            }
            relative /= std::filesystem::path(component);
        }

        if (separator == std::string::npos) {
            break;
        }
        component_start = separator + 1U;
    }

    std::error_code ec;
    const auto canonical_root = std::filesystem::weakly_canonical(root_, ec);
    if (ec) {
        error = "Unable to canonicalize the sandbox root";
        return false;
    }

    const auto candidate = std::filesystem::weakly_canonical(
        canonical_root / relative, ec);
    if (ec || !path_is_within(canonical_root, candidate)) {
        error = "Resolved guest path escapes the sandbox";
        return false;
    }

    host_path = candidate;
    error.clear();
    return true;
}

}  // namespace astrawin::filesystem
