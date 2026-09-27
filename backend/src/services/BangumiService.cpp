#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/repositories/MediaRepository.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace anime_vault {
namespace {
using TimePoint = std::chrono::system_clock::time_point;
constexpr std::size_t kMaxBody = 512 * 1024;

std::int64_t seconds(TimePoint value) {
    return std::chrono::duration_cast<std::chrono::seconds>(value.time_since_epoch()).count();
}
TimePoint fromSeconds(std::int64_t value) { return TimePoint{std::chrono::seconds{value}}; }

std::string normalizeQuery(const std::string& value) {
    std::string result;
    bool pendingSpace = false;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            if (!result.empty()) pendingSpace = true;
            continue;
        }
        if (pendingSpace) { result.push_back(' '); pendingSpace = false; }
        result.push_back(static_cast<char>(ch < 128 ? std::tolower(ch) : ch));
    }
    // Long Chinese Mikan titles can exceed 100 UTF-8 bytes while staying under the title limit.
    if (result.empty() || result.size() > 500) throw std::invalid_argument("invalid query length");
    // The JSON serializer rejects malformed UTF-8 before it reaches the transport or cache.
    (void)nlohmann::json(result).dump();
    return result;
}

std::string boundedString(const nlohmann::json& value, std::size_t limit) {
    if (!value.is_string()) throw std::invalid_argument("invalid subject string");
    auto text = value.get<std::string>();
    if (text.size() > limit) throw std::invalid_argument("oversized subject string");
    return text;
}

BangumiSubject parseSubject(const nlohmann::json& item) {
    if (!item.is_object() || !item.contains("id") || !item["id"].is_number_integer() ||
        !item.contains("type") || !item["type"].is_number_integer() ||
        !item.contains("name")) throw std::invalid_argument("invalid subject");
    BangumiSubject subject;
    subject.id = item["id"].get<std::int64_t>();
    subject.type = item["type"].get<int>();
    subject.name = boundedString(item["name"], 500);
    if (subject.id <= 0 || subject.name.empty() ||
        std::all_of(subject.name.begin(), subject.name.end(), [](unsigned char ch) { return std::isspace(ch); }) ||
        subject.type < 0 || subject.type > 20)
        throw std::invalid_argument("invalid subject fields");
    if (item.contains("name_cn") && !item["name_cn"].is_null())
        subject.nameCn = boundedString(item["name_cn"], 500);
    if (item.contains("date") && !item["date"].is_null())
        subject.date = boundedString(item["date"], 32);
    if (item.contains("eps") && !item["eps"].is_null()) {
        if (!item["eps"].is_number_integer()) throw std::invalid_argument("invalid episode count");
        subject.episodeCount = item["eps"].get<int>();
        if (subject.episodeCount < 0 || subject.episodeCount > 10000)
            throw std::invalid_argument("invalid episode count");
    }
    if (item.contains("images") && !item["images"].is_null()) {
        if (!item["images"].is_object()) throw std::invalid_argument("invalid images");
        for (const auto* field : {"large", "common", "grid"}) {
            if (item["images"].contains(field) && !item["images"][field].is_null()) {
                subject.coverUrl = boundedString(item["images"][field], 2048);
                break;
            }
        }
    }
    return subject;
}

std::vector<BangumiSubject> parseSearchBody(const std::string& body) {
    if (body.empty() || body.size() > kMaxBody) throw std::invalid_argument("invalid response size");
    auto document = nlohmann::json::parse(body);
    if (!document.is_object() || !document.contains("data") || !document["data"].is_array() ||
        document["data"].size() > 20) throw std::invalid_argument("invalid search response");
    std::vector<BangumiSubject> subjects;
    subjects.reserve(document["data"].size());
    for (const auto& item : document["data"]) subjects.push_back(parseSubject(item));
    return subjects;
}

BangumiSubject parseSubjectBody(const std::string& body) {
    if (body.empty() || body.size() > kMaxBody) throw std::invalid_argument("invalid response size");
    return parseSubject(nlohmann::json::parse(body));
}

bool retryable(int status) { return status == 429 || status >= 500; }
int backoffMinutes(int count) { return 1 << std::min(std::max(count - 1, 0), 4); }

template <typename Result, typename Callback>
auto once(Callback callback) {
    auto delivered = std::make_shared<std::atomic_bool>(false);
    auto ownedCallback = std::make_shared<Callback>(std::move(callback));
    return [delivered, ownedCallback](Result value) {
        if (!delivered->exchange(true)) (*ownedCallback)(std::move(value));
    };
}
}

BangumiService::BangumiService(MediaRepository& repository, BangumiTransport& transport, Clock clock)
    : repository_(repository), transport_(transport), clock_(std::move(clock)) {}

bool BangumiService::allowRequest(TimePoint now) {
    std::lock_guard lock(rateMutex_);
    std::erase_if(recentRequests_, [now](TimePoint at) { return at + std::chrono::minutes{1} <= now; });
    if (recentRequests_.size() >= 10) return false;
    recentRequests_.push_back(now);
    return true;
}

void BangumiService::search(BangumiMatchQuery query, SearchCompletion completion) {
    std::string normalized;
    try { normalized = normalizeQuery(query.title); }
    catch (...) { completion({{}, "bangumi_invalid_query"}); return; }
    try {
        if (const auto local = repository_.findAnimeByTitle(normalized);
            local && local->bangumiSubjectId && local->season.empty()) {
            BangumiSearchResult result;
            result.localMatch = BangumiSearchResult::LocalMatch{
                local->id, local->displayTitle, local->bangumiSubjectId};
            completion(std::move(result));
            return;
        }
    } catch (...) { completion({{}, "bangumi_storage_error"}); return; }
    const auto key = "search:v0:" + normalized;
    const auto now = clock_();
    std::optional<BangumiCacheRecord> cached;
    try { cached = repository_.getBangumiCache(key); }
    catch (...) { completion({{}, "bangumi_storage_error"}); return; }
    if (cached && cached->expiresAt > seconds(now)) {
        RankedCandidates ranked;
        try {
            auto subjects = parseSearchBody(cached->responseJson);
            query.title = normalized;
            ranked = rankBangumiCandidates(query, subjects);
        } catch (...) { completion({{}, "bangumi_bad_response"}); return; }
        completion({std::move(ranked), "", 0, std::nullopt, true});
        return;
    }
    if (cached && cached->retryAfter && *cached->retryAfter > seconds(now)) {
        completion({{}, "bangumi_unavailable", cached->retryCount, fromSeconds(*cached->retryAfter)});
        return;
    }
    if (!allowRequest(now)) { completion({{}, "bangumi_rate_limited"}); return; }
    auto self = shared_from_this();
    auto finish = once<BangumiSearchResult>(std::move(completion));
    try {
        transport_.search(normalized, [self, key, startedAt = seconds(now), query = std::move(query), cached = std::move(cached),
                                       finish](std::optional<BangumiTransport::Response> response,
                                                                         std::string) mutable {
            const auto callbackNow = self->clock_();
            const int status = response ? response->status : 0;
            if (!response || retryable(status)) {
                const int retries = std::min(cached ? cached->retryCount + 1 : 1, 5);
                const auto retryAt = callbackNow + std::chrono::minutes{backoffMinutes(retries)};
                try {
                    self->repository_.putBangumiFailureIfStale({key, cached ? cached->responseJson : "",
                        cached ? cached->expiresAt : 0, retries, seconds(retryAt)}, startedAt);
                } catch (...) { finish({{}, "bangumi_storage_error"}); return; }
                finish({{}, "bangumi_unavailable", retries, retryAt});
                return;
            }
            if (status != 200) { finish({{}, "bangumi_http_error"}); return; }
            try {
                auto subjects = parseSearchBody(response->body);
                self->repository_.putBangumiCache({key, response->body,
                    seconds(callbackNow + std::chrono::hours{24}), 0, std::nullopt});
                finish({rankBangumiCandidates(query, subjects)});
            } catch (const std::invalid_argument&) { finish({{}, "bangumi_bad_response"}); }
              catch (const nlohmann::json::exception&) { finish({{}, "bangumi_bad_response"}); }
              catch (...) { finish({{}, "bangumi_storage_error"}); }
        });
    } catch (...) { finish({{}, "bangumi_unavailable"}); }
}

void BangumiService::subject(std::int64_t id, SubjectCompletion completion) {
    if (id <= 0) { completion({std::nullopt, "bangumi_invalid_subject_id"}); return; }
    if (!allowRequest(clock_())) { completion({std::nullopt, "bangumi_rate_limited"}); return; }
    auto self = shared_from_this();
    auto finish = once<BangumiSubjectResult>(std::move(completion));
    try {
        transport_.subject(id, [self, finish, id](
                               std::optional<BangumiTransport::Response> response, std::string) mutable {
            if (!response || retryable(response->status)) {
                finish({std::nullopt, "bangumi_unavailable"}); return;
            }
            if (response->status != 200) {
                finish({std::nullopt, "bangumi_http_error"}); return;
            }
            try {
                auto subject = parseSubjectBody(response->body);
                if (subject.id != id) { finish({std::nullopt, "bangumi_bad_response"}); return; }
                if (subject.type != 2) { finish({std::nullopt, "bangumi_not_animation"}); return; }
                finish({std::move(subject), ""});
            } catch (...) { finish({std::nullopt, "bangumi_bad_response"}); }
        });
    } catch (...) { finish({std::nullopt, "bangumi_unavailable"}); }
}

} // namespace anime_vault
