#pragma once
#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/services/AnimeEnricher.hpp"
#include "anime_vault/services/MikanEnricher.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace anime_vault::api {

struct InboxPageHttpRequest {
    std::int64_t offset{};
    int limit{100};
    std::optional<std::string> origin;
};
void registerScanManagementEndpoints(MediaRepository& repository);
void registerScanController(MediaService& service, std::shared_ptr<AnimeEnricher> enricher,
                            std::shared_ptr<MikanEnricher> mikanEnricher);
}
