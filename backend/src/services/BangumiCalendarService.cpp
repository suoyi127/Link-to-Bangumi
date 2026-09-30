#include "anime_vault/services/BangumiCalendarService.hpp"

#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace anime_vault {
namespace {
constexpr const char* cacheKey = "calendar:weekly:v1";
constexpr std::int64_t cacheLifetime = 6 * 60 * 60;
constexpr std::int64_t staleLifetime = 7 * 24 * 60 * 60;
std::int64_t seconds(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}
}

BangumiCalendarService::BangumiCalendarService(MediaRepository& repository,
    BangumiTransport& transport, Clock clock)
    : repository_(repository), transport_(transport), clock_(std::move(clock)) {}

AnimeCalendarResult BangumiCalendarService::match(const std::string& body) const {
    if (body.empty() || body.size() > 512 * 1024) throw std::invalid_argument("invalid calendar size");
    const auto document = nlohmann::json::parse(body);
    if (!document.is_array() || document.size() > 7) throw std::invalid_argument("invalid calendar");
    std::array<std::set<std::int64_t>, 7> subjects;
    std::set<int> seenDays;
    for (const auto& day : document) {
        if (!day.is_object() || !day.contains("weekday") || !day["weekday"].is_object() ||
            !day["weekday"].contains("id") || !day["weekday"]["id"].is_number_integer() ||
            !day.contains("items") || !day["items"].is_array() || day["items"].size() > 2000)
            throw std::invalid_argument("invalid calendar day");
        const auto weekday = day["weekday"]["id"].get<int>();
        if (weekday < 1 || weekday > 7 || !seenDays.insert(weekday).second)
            throw std::invalid_argument("invalid weekday");
        for (const auto& item : day["items"]) {
            if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer() ||
                !item.contains("type") || !item["type"].is_number_integer())
                throw std::invalid_argument("invalid calendar subject");
            const auto id = item["id"].get<std::int64_t>();
            if (id <= 0) throw std::invalid_argument("invalid calendar subject id");
            if (item["type"].get<int>() == 2) subjects[weekday - 1].insert(id);
        }
    }

    std::set<std::int64_t> scheduled;
    for (const auto& ids : subjects) scheduled.insert(ids.begin(), ids.end());
    std::unordered_map<std::int64_t, AnimeRecord> local;
    // 遍历完整媒体库，避免只匹配当前页面；缺失文件与仅有元数据的词条不进入排期。
    std::int64_t offset = 0;
    while (true) {
        const auto page = repository_.listAnimePage(offset, 200);
        for (const auto& anime : page.items) {
            if (!anime.bangumiSubjectId || !scheduled.contains(*anime.bangumiSubjectId)) continue;
            const auto detail = repository_.getAnime(anime.id, 0, 1);
            if (detail && !detail->media.empty()) local.emplace(*anime.bangumiSubjectId, anime);
        }
        if (!page.nextOffset) break;
        offset = *page.nextOffset;
    }
    AnimeCalendarResult result{};
    for (std::size_t index = 0; index < subjects.size(); ++index) {
        result.days[index].weekday = static_cast<int>(index + 1);
        for (const auto id : subjects[index]) {
            const auto found = local.find(id);
            if (found != local.end()) result.days[index].items.push_back(found->second);
        }
    }
    return result;
}

void BangumiCalendarService::get(Completion completion) {
    std::optional<BangumiCacheRecord> cached;
    const auto now = seconds(clock_());
    try { cached = repository_.getBangumiCache(cacheKey); }
    catch (...) { completion({{}, false, false, 0, "calendar_storage_error"}); return; }
    if (cached && cached->expiresAt > now) {
        std::optional<AnimeCalendarResult> fresh;
        try {
            fresh = match(cached->responseJson);
            fresh->fromCache = true;
            fresh->updatedAt = cached->expiresAt - cacheLifetime;
        } catch (...) { cached.reset(); }
        if (fresh) { completion(std::move(*fresh)); return; }
    }
    auto self = shared_from_this();
    transport_.calendar([self, cached, now, completion = std::move(completion)](
        std::optional<BangumiTransport::Response> response, std::string error) mutable {
        if (response && response->status == 200 && error.empty()) {
            std::optional<AnimeCalendarResult> fresh;
            try {
                fresh = self->match(response->body);
                fresh->updatedAt = now;
            } catch (...) { error = "bangumi_calendar_bad_response"; }
            if (fresh) {
                // 缓存写入失败不阻止有效排期返回；回调不在解析异常捕获范围内。
                try { self->repository_.putBangumiCache({cacheKey, response->body, now + cacheLifetime, 0, {}}); }
                catch (...) {}
                completion(std::move(*fresh)); return;
            }
        }
        if (error != "bangumi_unconfigured" && error != "bangumi_calendar_bad_response")
            error = "bangumi_calendar_unavailable";
        // 断网时最多沿用七天内的排期，并明确标记过期，保留原缓存用于重试。
        if (cached && now - (cached->expiresAt - cacheLifetime) <= staleLifetime) {
            std::optional<AnimeCalendarResult> stale;
            try {
                stale = self->match(cached->responseJson);
                stale->fromCache = true;
                stale->stale = true;
                stale->updatedAt = cached->expiresAt - cacheLifetime;
                stale->errorCode = error;
            } catch (...) {}
            if (stale) { completion(std::move(*stale)); return; }
        }
        completion({{}, false, false, 0, error});
    });
}
}
