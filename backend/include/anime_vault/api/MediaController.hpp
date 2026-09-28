#pragma once

#include "anime_vault/services/OrganizationService.hpp"
#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/services/AnimeEnricher.hpp"
#include "anime_vault/services/MikanEnricher.hpp"
#include "anime_vault/services/CoverScraper.hpp"
#include "anime_vault/services/PlaybackService.hpp"

#include <json/json.h>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace anime_vault::api {
class MediaService;
struct InboxPageHttpRequest {
    std::int64_t offset{};
    int limit{100};
    std::optional<std::string> origin;
};
struct ExecuteOrganizationHttpRequest {
    std::int64_t planId{};
    std::string idempotencyKey;
    bool confirmed{};
    bool qbDownloadComplete{};
};
struct BindAnimeHttpRequest {
    std::int64_t subjectId{};
    bool confirmed{};
};
struct PlayMediaHttpRequest { std::int64_t mediaId{}; };
PlayMediaHttpRequest parsePlayMediaRequest(std::string_view rawId);
bool playbackOriginAllowed(std::string_view origin);
ExecuteOrganizationHttpRequest parseExecuteOrganizationRequest(const Json::Value& body);
BindAnimeHttpRequest parseBindAnimeRequest(const Json::Value& body);
struct EffectiveSettings {
    std::filesystem::path sourcePath, importPath, libraryPath, dataPath;
    bool bangumiConfigured{};
    bool qbConfigured{};
    bool qbDownloadConfigured{};
    bool qbDownloadEnvironmentOverride{};
    std::function<bool()> qbConfiguredNow;
    std::function<bool()> bangumiConfiguredNow;
};
struct QbDownloadDirectoryRequest { std::string path; };
QbDownloadDirectoryRequest parseQbDownloadDirectoryRequest(const Json::Value& body);
UiPreferences parseUiPreferencesRequest(const Json::Value& body);
Json::Value executeOrganizationJson(const ExecuteOrganizationResult& result);
void registerManagementEndpoints(MediaRepository& repository, EffectiveSettings settings);
void registerMediaEndpoints(MediaService& service, OrganizationService& organization,
                            std::shared_ptr<AnimeEnricher> enricher = {},
                            std::shared_ptr<MikanEnricher> mikanEnricher = {});
void registerAnimeEndpoints(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                            std::shared_ptr<CoverScraper> covers = {});
void registerPlaybackEndpoint(MediaRepository& repository, PlaybackService& playback);
}
