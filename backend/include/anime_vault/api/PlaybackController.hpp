#pragma once
#include "anime_vault/ports/SettingsRepository.hpp"
#include "anime_vault/services/PlaybackService.hpp"
#include <string_view>

namespace anime_vault::api {
struct PlayMediaHttpRequest { std::int64_t mediaId{}; };
PlayMediaHttpRequest parsePlayMediaRequest(std::string_view rawId);
bool playbackOriginAllowed(std::string_view origin);
void registerPlaybackController(SettingsRepository& repository, PlaybackService& playback);
}
