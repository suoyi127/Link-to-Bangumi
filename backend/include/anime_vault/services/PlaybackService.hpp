#pragma once

#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include "anime_vault/repositories/MediaRepository.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace anime_vault {

class PlaybackError final : public std::runtime_error {
public:
    explicit PlaybackError(std::string code)
        : std::runtime_error(code), code_(std::move(code)) {}
    const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

class PlaybackService {
public:
    PlaybackService(MediaRepository& repository, std::filesystem::path sourceRoot,
                    std::filesystem::path importRoot, std::filesystem::path libraryRoot,
                    ProcessLauncher& launcher);
    void play(std::int64_t mediaId, const std::string& mpvExecutable) const;
private:
    MediaRepository& repository_;
    std::filesystem::path sourceRoot_, importRoot_, libraryRoot_;
    ProcessLauncher& launcher_;
};

} // namespace anime_vault
