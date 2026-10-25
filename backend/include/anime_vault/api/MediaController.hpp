#pragma once
#include "anime_vault/api/ScanController.hpp"
#include "anime_vault/api/AnimeController.hpp"
#include "anime_vault/api/OrganizationController.hpp"
#include "anime_vault/api/PlaybackController.hpp"
#include "anime_vault/api/SettingsController.hpp"

namespace anime_vault::api {
// 保留现有注册入口，调用方可逐步迁移到各职责控制器。
void registerManagementEndpoints(MediaRepository& repository, EffectiveSettings settings);
void registerMediaEndpoints(MediaService& service, OrganizationService& organization,
                            std::shared_ptr<AnimeEnricher> enricher = {},
                            std::shared_ptr<MikanEnricher> mikanEnricher = {});
void registerAnimeEndpoints(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                            std::shared_ptr<CoverScraper> covers = {},
                            std::shared_ptr<BangumiCalendarService> calendar = {});
void registerPlaybackEndpoint(MediaRepository& repository, PlaybackService& playback);
}
