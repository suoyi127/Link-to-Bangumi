#include "anime_vault/services/RuntimePaths.hpp"

#include <cstdlib>
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
} // namespace

fs::path defaultDataDirectory() {
    auto local = environmentPath("LOCALAPPDATA", L"LOCALAPPDATA");
    if (!local.empty()) return local / "AnimeVault";
    auto profile = environmentPath("USERPROFILE", L"USERPROFILE");
    if (!profile.empty()) return profile / "AppData" / "Local" / "AnimeVault";
    return fs::temp_directory_path() / "AnimeVault";
}

fs::path defaultMediaDirectory() {
    auto profile = environmentPath("USERPROFILE", L"USERPROFILE");
#ifndef _WIN32
    if (profile.empty()) profile = environmentPath("HOME", L"HOME");
#endif
    if (!profile.empty()) return profile / "Videos" / "AnimeVault";
    return defaultDataDirectory() / "Media";
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
} // namespace anime_vault
