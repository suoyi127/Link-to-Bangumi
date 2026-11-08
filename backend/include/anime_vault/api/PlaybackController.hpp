#pragma once
#include "anime_vault/ports/SettingsRepository.hpp"
#include "anime_vault/services/PlaybackService.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace anime_vault::api {
struct PlayMediaHttpRequest {
    std::int64_t mediaId{};
    std::optional<std::string> playerId;
    bool fromStart{};
};
PlayMediaHttpRequest parsePlayMediaRequest(std::string_view rawId, std::string_view body = {});
bool playbackOriginAllowed(std::string_view origin);
void registerPlaybackController(SettingsRepository& repository, PlaybackService& playback);
}
