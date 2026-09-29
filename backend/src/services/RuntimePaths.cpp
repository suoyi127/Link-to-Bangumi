#include "anime_vault/services/RuntimePaths.hpp"

#include <cstdlib>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>

namespace anime_vault {
namespace fs = std::filesystem;
namespace {
fs::path environmentPath(const char* narrow, const wchar_t* wide) {
#ifdef _WIN32
    (void)narrow;
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, wide) == 0) {
        const std::unique_ptr<wchar_t, decltype(&std::free)> owned(value, &std::free);
        if (value && *value) return fs::path(value);
    }
#else
    (void)wide;
    if (const auto* value = std::getenv(narrow); value && *value) return fs::path(value);
#endif
    return {};
}
bool within(const fs::path& root, const fs::path& candidate) {
    auto parent = root.begin();
    auto child = candidate.begin();
    for (; parent != root.end(); ++parent, ++child) {
        if (child == candidate.end()) return false;
#ifdef _WIN32
        auto left = parent->wstring();
        auto right = child->wstring();
        std::transform(left.begin(), left.end(), left.begin(), std::towlower);
        std::transform(right.begin(), right.end(), right.begin(), std::towlower);
        if (left != right) return false;
#else
        if (*parent != *child) return false;
#endif
    }
    return true;
}
} // namespace

fs::path defaultDataDirectory() {
    auto local = environmentPath("LOCALAPPDATA", L"LOCALAPPDATA");
    if (!local.empty()) return local / "AnimeVault";
    auto profile = environmentPath("USERPROFILE", L"USERPROFILE");
    if (!profile.empty()) return profile / "AppData" / "Local" / "AnimeVault";
    return fs::temp_directory_path() / "AnimeVault";
}

fs::path defaultMediaDirectory() {
    const auto root = environmentPath("ANIME_VAULT_HOME", L"ANIME_VAULT_HOME");
    return defaultMediaDirectory(root.empty() ? fs::current_path() : root);
}

fs::path defaultMediaDirectory(const fs::path& applicationRoot) {
    return fs::absolute(applicationRoot).lexically_normal() / "Media";
}

RuntimePaths resolveRuntimePaths(fs::path data, fs::path imported, fs::path library,
                                 std::optional<fs::path> qbOverride, std::string_view storedQb) {
    RuntimePaths result{fs::absolute(std::move(data)).lexically_normal(), {},
        fs::absolute(std::move(imported)).lexically_normal(),
        fs::absolute(std::move(library)).lexically_normal(), false};
    if (qbOverride && !qbOverride->empty()) {
        result.source = fs::absolute(*qbOverride).lexically_normal();
        result.qbConfigured = true;
    } else if (!storedQb.empty()) {
        const auto* bytes = reinterpret_cast<const char8_t*>(storedQb.data());
        result.source = fs::absolute(fs::path(std::u8string_view(bytes, storedQb.size()))).lexically_normal();
        result.qbConfigured = true;
    } else {
        // The private empty source keeps existing service constructors valid; API gates prevent scanning it.
        result.source = result.data / "unconfigured-qb-source";
    }
    return result;
}

fs::path validateQbDownloadDirectory(const fs::path& candidate, const fs::path& imported,
                                     const fs::path& library, const fs::path& data) {
    if (candidate.empty() || !candidate.is_absolute())
        throw RuntimePathError("invalid_qb_download_directory", "absolute qB directory required");
    std::error_code error;
    if (!fs::is_directory(candidate, error) || error)
        throw RuntimePathError("invalid_qb_download_directory", "qB directory must exist");
    const auto selected = fs::canonical(candidate, error);
    if (error || selected == selected.root_path())
        throw RuntimePathError("invalid_qb_download_directory", "filesystem root is not allowed");
    // Resolve existing ancestors so junctions and symlinks cannot conceal overlap.
    for (const auto& protectedRoot : {imported, library, data}) {
        const auto resolved = fs::weakly_canonical(fs::absolute(protectedRoot), error);
        if (error) throw RuntimePathError("invalid_qb_download_directory", "cannot resolve protected root");
        if (within(selected, resolved) || within(resolved, selected))
            throw RuntimePathError("overlapping_roots", "qB directory overlaps another application root");
    }
    return selected;
}

bool qbDirectoryActive(const fs::path& active, std::string_view stored,
                       bool environmentOverride) {
    if (environmentOverride) return true;
    if (stored.empty()) return environmentOverride;
    const auto* bytes = reinterpret_cast<const char8_t*>(stored.data());
    std::error_code error;
    const auto selected = fs::path(std::u8string_view(bytes, stored.size()));
    return fs::equivalent(active, selected, error) && !error;
}
} // namespace anime_vault
