#include "anime_vault/services/TargetPlanner.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace anime_vault {
namespace fs = std::filesystem;

namespace {
bool within(const fs::path& root, const fs::path& candidate) {
    return std::mismatch(root.begin(), root.end(), candidate.begin(), candidate.end()).first == root.end();
}

fs::path canonicalExistingPrefix(fs::path path) {
    path = fs::absolute(path).lexically_normal();
    fs::path suffix;
    while (!fs::exists(path)) {
        if (path == path.root_path()) throw fs::filesystem_error("no existing path ancestor", path, std::make_error_code(std::errc::no_such_file_or_directory));
        suffix = path.filename() / suffix;
        path = path.parent_path();
    }
    const auto resolved = fs::canonical(path);
    return suffix.empty() ? resolved : (resolved / suffix).lexically_normal();
}

void validateComponent(std::string_view value) {
    if (value.empty() || value == "." || value == ".." || value.back() == '.' || value.back() == ' ') {
        throw std::invalid_argument("invalid target path component");
    }
    for (unsigned char ch : value) {
        if (ch < 32 || ch == 127 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) {
            throw std::invalid_argument("invalid target path component");
        }
    }
    const auto stem = value.substr(0, value.find('.'));
    std::string upper(stem);
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
        (upper.size() == 4 && ((upper.substr(0, 3) == "COM") || (upper.substr(0, 3) == "LPT")) &&
         upper[3] >= '1' && upper[3] <= '9')) {
        throw std::invalid_argument("reserved Windows path component");
    }
}

std::string episodeStem(EpisodeType type, const EpisodeNumber& episode) {
    std::string prefix;
    switch (type) {
    case EpisodeType::normal: break;
    case EpisodeType::sp: prefix = "SP"; break;
    case EpisodeType::ova: prefix = "OVA"; break;
    case EpisodeType::ncop: prefix = "NCOP"; break;
    case EpisodeType::nced: prefix = "NCED"; break;
    case EpisodeType::unknown: throw std::invalid_argument("episode type is unknown");
    }
    std::ostringstream stream;
    stream << prefix << std::setw(2) << std::setfill('0') << episode.whole();
    if (episode.tenth()) stream << '.' << *episode.tenth();
    return stream.str();
}
}  // namespace

TargetPlanner::TargetPlanner(fs::path libraryRoot) {
    if (libraryRoot.empty()) throw std::invalid_argument("library root is empty");
    requestedRoot_ = fs::absolute(libraryRoot).lexically_normal();
    root_ = canonicalExistingPrefix(requestedRoot_);
    if (fs::exists(root_) && !fs::is_directory(root_)) {
        throw std::invalid_argument("library root must be a directory");
    }
}

TargetPlan TargetPlanner::preview(const SourceFile& source, std::string_view displayTitle,
                                  EpisodeType type, const EpisodeNumber& episode,
                                  std::string_view versionLabel) const {
    if (fs::is_symlink(requestedRoot_) ||
        (fs::exists(requestedRoot_) && !fs::is_directory(requestedRoot_)) ||
        canonicalExistingPrefix(requestedRoot_) != root_) {
        throw std::invalid_argument("library root changed or is not a directory");
    }
    validateComponent(displayTitle);
    if (!versionLabel.empty()) validateComponent(versionLabel);
    auto extension = source.path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    constexpr std::array extensions{".mkv", ".mp4", ".avi", ".mov", ".wmv",
                                    ".m4v", ".ts", ".flv", ".webm"};
    if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end()) {
        throw std::invalid_argument("unsupported video extension");
    }
    auto name = std::string(displayTitle) + " [" + episodeStem(type, episode) + "]";
    if (!versionLabel.empty()) name += "-" + std::string(versionLabel);
    const auto target = root_ / std::string(displayTitle) / (name + extension);
    const auto titlePath = root_ / std::string(displayTitle);
    if (fs::is_symlink(titlePath)) {
        throw std::invalid_argument("title directory must not be a symlink");
    }
    TargetPlan plan{source.path, target};
    if (fs::exists(titlePath) && !fs::is_directory(titlePath)) {
        plan.conflicts.emplace_back("parent_not_directory");
        return plan;
    }
    const auto resolvedTitle = fs::exists(titlePath) ? fs::canonical(titlePath) : titlePath;
    const auto resolved = fs::exists(target) ? fs::canonical(target) : resolvedTitle / target.filename();
    if (!within(root_, resolved)) throw std::invalid_argument("target escapes library root");
    if (fs::exists(target) || fs::is_symlink(target)) plan.conflicts.emplace_back("target_exists");
    return plan;
}

}  // namespace anime_vault
