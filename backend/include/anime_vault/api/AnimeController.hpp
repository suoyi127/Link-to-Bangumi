#pragma once
#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/services/BangumiCalendarService.hpp"
#include "anime_vault/services/CoverScraper.hpp"
#include <json/json.h>

namespace anime_vault::api {
struct BindAnimeHttpRequest {
    std::int64_t subjectId{};
    bool confirmed{};
};
struct PreviewAnimeMergeHttpRequest { std::int64_t subjectId{}; };
struct ConfirmAnimeMergeHttpRequest { std::string previewId; bool confirmed{}; };
BindAnimeHttpRequest parseBindAnimeRequest(const Json::Value& body);
void registerAnimeController(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                            std::shared_ptr<CoverScraper> covers,
                            std::shared_ptr<BangumiCalendarService> calendar);
}
