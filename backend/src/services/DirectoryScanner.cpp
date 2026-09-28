#include "anime_vault/services/DirectoryScanner.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace anime_vault {
namespace fs = std::filesystem;

namespace {
bool within(const fs::path& root, const fs::path& candidate) {
    return std::mismatch(root.begin(), root.end(), candidate.begin(), candidate.end()).first == root.end();
}

bool supported(const fs::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    constexpr std::array extensions{".mkv", ".mp4", ".avi", ".mov", ".wmv",
                                    ".m4v", ".ts", ".flv", ".webm"};
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

bool isReparsePoint(const fs::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    (void)path;
    return false;
#endif
}
}  // namespace

DirectoryScanner::DirectoryScanner(fs::path root) : root_(fs::canonical(std::move(root))) {
    if (!fs::is_directory(root_)) {
        throw std::invalid_argument("scanner root must be a directory");
    }
}

std::vector<SourceFile> DirectoryScanner::scan(std::chrono::seconds stableFor,
                                                fs::file_time_type now) {
    if (stableFor < std::chrono::seconds::zero()) {
        throw std::invalid_argument("stability window must not be negative");
    }
    // 当前快照会替换上一轮快照；只有元数据连续稳定达到窗口后才交付文件。
    std::vector<SourceFile> files;
    std::map<fs::path, Snapshot> current;
    for (fs::recursive_directory_iterator iterator(root_), end; iterator != end; ++iterator) {
        const auto& entry = *iterator;
        // 不跟随符号链接或 Windows 重解析点，避免扫描根目录之外的内容。
        if (entry.is_symlink() || isReparsePoint(entry.path())) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (!supported(entry.path()) || !entry.is_regular_file()) {
            continue;
        }
        const auto resolved = fs::canonical(entry.path());
        if (!within(root_, resolved)) {
            continue;
        }
        const auto modifiedAt = entry.last_write_time();
        const auto size = entry.file_size();
        const auto previous = snapshots_.find(resolved);
        const auto firstSeen = previous != snapshots_.end() &&
                                       previous->second.size == size &&
                                       previous->second.modifiedAt == modifiedAt
                                   ? previous->second.firstSeen
                                   : now;
        current.emplace(resolved, Snapshot{size, modifiedAt, firstSeen});
        if (modifiedAt <= now - stableFor && firstSeen <= now - stableFor) {
            files.push_back({resolved, size, modifiedAt});
        }
    }
    snapshots_.swap(current);
    return files;
}

std::vector<fs::path> DirectoryScanner::observedPaths() const {
    std::vector<fs::path> paths;
    paths.reserve(snapshots_.size());
    for (const auto& [path, _] : snapshots_) paths.push_back(path);
    return paths;
}

}  // namespace anime_vault
