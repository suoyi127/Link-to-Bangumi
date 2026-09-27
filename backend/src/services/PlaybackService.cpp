#include "anime_vault/services/PlaybackService.hpp"

#include <algorithm>
#include <system_error>

namespace anime_vault {
namespace fs = std::filesystem;
namespace {
fs::path fromUtf8(const std::string& text) {
    const auto* bytes = reinterpret_cast<const char8_t*>(text.data());
    return fs::path(std::u8string_view(bytes, text.size()));
}
std::string toUtf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
bool within(const fs::path& root, const fs::path& candidate) {
    return std::mismatch(root.begin(), root.end(), candidate.begin(), candidate.end()).first == root.end();
}
}

PlaybackService::PlaybackService(MediaRepository& repository, fs::path sourceRoot,
                                 fs::path importRoot, fs::path libraryRoot,
                                 ProcessLauncher& launcher)
    : repository_(repository), sourceRoot_(std::move(sourceRoot)),
      importRoot_(std::move(importRoot)), libraryRoot_(std::move(libraryRoot)),
      launcher_(launcher) {}

void PlaybackService::play(std::int64_t mediaId, const std::string& mpvExecutable) const {
    if (mediaId <= 0) throw PlaybackError("invalid_media_id");
    const auto media = repository_.getMedia(mediaId);
    if (!media) throw PlaybackError("media_not_found");
    if (mpvExecutable.empty()) throw PlaybackError("mpv_not_configured");
    const auto executable = fromUtf8(mpvExecutable);
    std::error_code ec;
    if (!executable.is_absolute() || !fs::is_regular_file(executable, ec))
        throw PlaybackError("mpv_not_found");
    const auto resolvedExecutable = fs::canonical(executable, ec);
    if (ec) throw PlaybackError("mpv_not_found");

    const bool organized = !media->libraryPath.empty();
    const fs::path& allowedRoot = organized ? libraryRoot_ :
        media->origin == "qb_download" ? sourceRoot_ : importRoot_;
    if (!organized && media->origin != "qb_download" && media->origin != "external_import")
        throw PlaybackError("playback_origin_invalid");
    const auto path = fromUtf8(organized ? media->libraryPath : media->sourcePath);
    if (!path.is_absolute() || !fs::is_regular_file(path, ec))
        throw PlaybackError("playback_file_missing");
    const auto canonicalPath = fs::canonical(path, ec);
    if (ec) throw PlaybackError("playback_file_missing");
    const auto canonicalRoot = fs::canonical(allowedRoot, ec);
    if (ec || !within(canonicalRoot, canonicalPath))
        throw PlaybackError("playback_path_outside_root");
    // The `--` boundary prevents a filename beginning with '-' from becoming an mpv option.
    if (!launcher_.launch(resolvedExecutable,
                          {"--save-position-on-quit", "--", toUtf8(canonicalPath)}))
        throw PlaybackError("mpv_launch_failed");
}

} // namespace anime_vault
