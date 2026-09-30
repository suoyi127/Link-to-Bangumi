#include "anime_vault/services/PlaybackService.hpp"
#include "anime_vault/services/PlayerCatalog.hpp"

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

void PlaybackService::play(std::int64_t mediaId, const std::string& mpvExecutable, const std::string& playerType) const {
    if (mediaId <= 0) throw PlaybackError("invalid_media_id");
    if (!validPlayerType(playerType)) throw PlaybackError("invalid_player");
    const auto media = repository_.getMedia(mediaId);
    if (!media) throw PlaybackError("media_not_found");
    std::error_code ec;
    fs::path resolvedExecutable;
    if (playerType != "system") {
        if (mpvExecutable.empty()) throw PlaybackError(playerType == "mpv" ? "mpv_not_configured" : "player_not_configured");
        const auto executable = fromUtf8(mpvExecutable);
        if (!validPlayerExecutable(mpvExecutable))
            throw PlaybackError(playerType == "mpv" ? "mpv_not_found" : "player_not_found");
        resolvedExecutable = fs::canonical(executable, ec);
        if (ec) throw PlaybackError("player_not_found");
    }

    const bool organized = !media->libraryPath.empty();
    fs::path allowedRoot = organized ? libraryRoot_ :
        media->origin == "qb_download" ? sourceRoot_ : importRoot_;
    if (!organized && media->origin != "qb_download" && media->origin != "external_import" &&
        media->origin != "folder_import")
        throw PlaybackError("playback_origin_invalid");
    if (!organized && media->origin == "folder_import") {
        const auto folder = media->folderImportId
            ? repository_.getFolderImport(*media->folderImportId) : std::nullopt;
        if (!folder) throw PlaybackError("playback_origin_invalid");
        allowedRoot = fromUtf8(folder->rootPath);
        if (!fs::is_directory(allowedRoot, ec) || ec ||
            fs::canonical(allowedRoot, ec) != allowedRoot || ec)
            throw PlaybackError("playback_path_outside_root");
    }
    const auto path = fromUtf8(organized ? media->libraryPath : media->sourcePath);
    if (!path.is_absolute() || !fs::is_regular_file(path, ec))
        throw PlaybackError("playback_file_missing");
    const auto canonicalPath = fs::canonical(path, ec);
    if (ec) throw PlaybackError("playback_file_missing");
    const auto canonicalRoot = fs::canonical(allowedRoot, ec);
    if (ec || !within(canonicalRoot, canonicalPath))
        throw PlaybackError("playback_path_outside_root");
    auto extension = canonicalPath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const std::vector<std::string> formats{".mkv", ".mp4", ".avi", ".mov", ".webm", ".m4v", ".ts", ".m2ts", ".wmv", ".flv", ".mpg", ".mpeg", ".ogm"};
    if (std::find(formats.begin(), formats.end(), extension) == formats.end())
        throw PlaybackError("playback_format_unsupported");
    if (playerType == "system") {
        if (!launcher_.openDefault(canonicalPath)) throw PlaybackError("player_launch_failed");
        return;
    }
    // 固定参数配置；临时选择只改变播放器，不允许网页提交命令行。
    std::vector<std::string> arguments{toUtf8(canonicalPath)};
    if (playerType == "mpv") arguments = {"--save-position-on-quit", "--", toUtf8(canonicalPath)};
    if (!launcher_.launch(resolvedExecutable, arguments))
        throw PlaybackError(playerType == "mpv" ? "mpv_launch_failed" : "player_launch_failed");
}

} // namespace anime_vault
