#include "anime_vault/api/MediaController.hpp"
#include "anime_vault/api/ScanController.hpp"
#include "anime_vault/api/AnimeController.hpp"
#include "anime_vault/api/OrganizationController.hpp"
#include "anime_vault/api/PlaybackController.hpp"
#include "anime_vault/api/SettingsController.hpp"

namespace anime_vault::api {
void registerManagementEndpoints(MediaRepository& repository, EffectiveSettings settings) {
    registerScanManagementEndpoints(repository);
    registerSettingsController(repository, std::move(settings));
}
void registerMediaEndpoints(MediaService& service, OrganizationService& organization,
                            std::shared_ptr<AnimeEnricher> enricher,
                            std::shared_ptr<MikanEnricher> mikanEnricher) {
    registerScanController(service, std::move(enricher), std::move(mikanEnricher));
    registerOrganizationController(service, organization);
}
void registerAnimeEndpoints(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                            std::shared_ptr<CoverScraper> covers,
                            std::shared_ptr<BangumiCalendarService> calendar) {
    registerAnimeController(repository, std::move(bangumi), std::move(covers), std::move(calendar));
}
void registerPlaybackEndpoint(MediaRepository& repository, PlaybackService& playback) {
    registerPlaybackController(repository, playback);
}
}
