#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/repositories/MediaRepository.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <limits>
#include <cmath>
#include <unordered_set>
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
    if (item.contains("infobox") && item["infobox"].is_array()) {
        for (const auto& field : item["infobox"]) {
            if (!field.is_object() || field.value("key", "") != "别名" || !field.contains("value")) continue;
            const auto add = [&subject](const nlohmann::json& value) {
                if (value.is_string()) subject.aliases.push_back(boundedString(value, 500));
                else if (value.is_object() && value.contains("v") && value["v"].is_string())
                    subject.aliases.push_back(boundedString(value["v"], 500));
            };
            if (field["value"].is_array()) {
                for (const auto& value : field["value"]) {
                    if (subject.aliases.size() >= 64) break;
                    add(value);
                }
            } else add(field["value"]);
        }
    }
    return subject;
}

std::optional<std::int64_t> uniqueLegacyId(const std::string& body) {
    if (body.empty() || body.size() > kMaxBody) throw std::invalid_argument("invalid alias response size");
    const auto document = nlohmann::json::parse(body);
    if (!document.is_object() || !document.contains("results") || !document["results"].is_number_integer())
        throw std::invalid_argument("invalid alias response");
    const auto count = document["results"].get<int>();
    if (count < 0) throw std::invalid_argument("invalid alias count");
    if (count > 1) return std::int64_t{-1};
    if (count == 0) return std::nullopt;
    if (!document.contains("list") || !document["list"].is_array() || document["list"].size() != 1 ||
        !document["list"][0].contains("id") || !document["list"][0]["id"].is_number_integer())
        throw std::invalid_argument("invalid alias result");
    const auto id = document["list"][0]["id"].get<std::int64_t>();
    if (id <= 0) throw std::invalid_argument("invalid alias ID");
    return id;
}

bool verifiedAlias(const std::string& title, const BangumiSubject& subject) {
    const auto key = normalizeTitle(title);
    if (key.size() < 12) return false;
    const auto matches = [&key](const std::string& candidate) {
        const auto alias = normalizeTitle(candidate);
        if (alias == key) return true;
        if (alias.size() >= key.size() && std::equal(key.begin(), key.end(), alias.begin())) return true;
        // 罗马音转写中的一两个字母差异可以接受，但短名称不能模糊绑定。
        if (key.size() < 24 || alias.size() + 2 < key.size() || key.size() + 2 < alias.size()) return false;
        std::vector<std::size_t> previous(alias.size() + 1), current(alias.size() + 1);
        for (std::size_t j = 0; j <= alias.size(); ++j) previous[j] = j;
        for (std::size_t i = 1; i <= key.size(); ++i) {
            current[0] = i;
            for (std::size_t j = 1; j <= alias.size(); ++j)
                current[j] = std::min({previous[j] + 1, current[j - 1] + 1,
                    previous[j - 1] + (key[i - 1] == alias[j - 1] ? 0u : 1u)});
            std::swap(previous, current);
        }
        return previous.back() <= 2;
    };
    if (matches(subject.name) || matches(subject.nameCn)) return true;
    return std::any_of(subject.aliases.begin(), subject.aliases.end(), matches);
}

std::string distinctivePrefix(const std::string& title) {
    if (title.size() < 30 || !std::all_of(title.begin(), title.end(),
            [](unsigned char ch) { return ch < 0x80; })) return {};
    std::size_t words = 0;
    for (std::size_t i = 0; i < title.size(); ++i) {
        if (title[i] == ' ' && ++words == 6 && i >= 24) return title.substr(0, i);
    }
    return {};
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

BangumiService::BangumiService(MediaRepository& repository, BangumiTransport& transport, Clock clock, Scheduler scheduler)
    : scheduler_(std::move(scheduler)), repository_(repository), transport_(transport), clock_(std::move(clock)) {}

bool BangumiService::allowRequest(TimePoint now) {
    std::lock_guard lock(rateMutex_);
    std::erase_if(recentRequests_, [now](TimePoint at) { return at + std::chrono::minutes{1} <= now; });
    if (recentRequests_.size() >= 10) return false;
    recentRequests_.push_back(now);
    return true;
}

void BangumiService::remoteRateLimited() {
    std::lock_guard lock(rateMutex_);
    remoteRetryAt_ = std::max(remoteRetryAt_, clock_() + std::chrono::minutes{1});
}

void BangumiService::scheduleRequest(Priority priority, std::function<void()> action,
                                    std::function<void(std::string)> rejected) {
    // 未注入事件循环的嵌入调用仍明确区分本地预算；生产服务始终注入非阻塞定时器。
    if (!scheduler_) {
        bool cooling;
        { std::lock_guard lock(rateMutex_); cooling = remoteRetryAt_ > clock_(); }
        if (cooling) rejected("bangumi_remote_rate_limited");
        else if (allowRequest(clock_())) action();
        else rejected("bangumi_local_rate_limited");
        return;
    }
    bool full;
    {
        std::lock_guard lock(rateMutex_);
        full = manualQueue_.size() + backgroundQueue_.size() >= 128;
        if (!full) (priority == Priority::manual ? manualQueue_ : backgroundQueue_).push_back({std::move(action)});
    }
    if (full) { rejected("bangumi_queue_full"); return; }
    pumpRequests();
}

void BangumiService::pumpRequests() {
    {
        std::lock_guard lock(rateMutex_);
        if (pumping_) return;
        pumping_ = true;
    }
    for (;;) {
        std::function<void()> action;
        std::optional<std::chrono::milliseconds> delay;
        {
            std::lock_guard lock(rateMutex_);
            const auto now = clock_();
            std::erase_if(recentRequests_, [now](TimePoint at) { return at + std::chrono::minutes{1} <= now; });
            auto& queue = manualQueue_.empty() ? backgroundQueue_ : manualQueue_;
            // 后台最多使用8个槽位；手动请求优先消费队列且可使用完整10个槽位。
            const auto limit = manualQueue_.empty() ? 8u : 10u;
            if (!queue.empty() && remoteRetryAt_ <= now && recentRequests_.size() < limit) {
                action = std::move(queue.front().action); queue.pop_front(); recentRequests_.push_back(now);
            } else {
                pumping_ = false;
                if (!queue.empty() && !timerArmed_) {
                    auto wake = remoteRetryAt_;
                    if (recentRequests_.size() >= limit) wake = std::max(wake, recentRequests_.front() + std::chrono::minutes{1});
                    delay = std::max(std::chrono::milliseconds{1}, std::chrono::duration_cast<std::chrono::milliseconds>(wake - now));
                    timerArmed_ = true;
                }
            }
        }
        if (action) { action(); continue; }
        if (delay) {
            const auto weak = weak_from_this();
            scheduler_(*delay, [weak] {
                if (const auto self = weak.lock()) {
                    { std::lock_guard lock(self->rateMutex_); self->timerArmed_ = false; }
                    self->pumpRequests();
                }
            });
        }
        return;
    }
}

void BangumiService::search(BangumiMatchQuery query, SearchCompletion completion, Priority priority) {
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
    auto self = shared_from_this();
    auto finish = once<BangumiSearchResult>(std::move(completion));
    scheduleRequest(priority, [self, normalized, key, now, query = std::move(query), cached = std::move(cached), finish]() mutable {
    try {
        self->transport_.search(normalized, [self, key, startedAt = seconds(now), query = std::move(query), cached = std::move(cached),
                                       finish](std::optional<BangumiTransport::Response> response,
                                                                         std::string) mutable {
            const auto callbackNow = self->clock_();
            const int status = response ? response->status : 0;
            if (status == 429) { self->remoteRateLimited(); finish({{}, "bangumi_remote_rate_limited", 0, self->clock_() + std::chrono::minutes{1}}); return; }
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
    }, [finish](std::string code) { finish({{}, std::move(code)}); });
}

void BangumiService::subject(std::int64_t id, SubjectCompletion completion, Priority priority) {
    if (id <= 0) { completion({std::nullopt, "bangumi_invalid_subject_id"}); return; }
    std::optional<BangumiSubject> cached;
    {
        std::lock_guard lock(rateMutex_);
        const auto found = subjectCache_.find(id);
        if (found != subjectCache_.end() && found->second.first > clock_()) cached = found->second.second;
    }
    if (cached) { completion({std::move(cached), ""}); return; }
    auto self = shared_from_this();
    auto finish = once<BangumiSubjectResult>([self, id, completion = std::move(completion)](BangumiSubjectResult result) mutable {
        if (result.subject && result.errorCode.empty()) {
            std::lock_guard lock(self->rateMutex_);
            if (self->subjectCache_.size() >= 256) self->subjectCache_.clear();
            self->subjectCache_[id] = {self->clock_() + std::chrono::minutes{result.subject->episodes.empty() ? 1 : 15}, *result.subject};
        }
        completion(std::move(result));
    });
    scheduleRequest(priority, [self, id, finish, priority] {
    try {
        self->transport_.subject(id, [self, finish, id, priority](
                               std::optional<BangumiTransport::Response> response, std::string) mutable {
            if (response && response->status == 429) { self->remoteRateLimited(); finish({std::nullopt, "bangumi_remote_rate_limited"}); return; }
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
                self->loadEpisodes(std::move(subject), 0, -1, finish, 0, priority);
            } catch (...) { finish({std::nullopt, "bangumi_bad_response"}); }
        });
    } catch (...) { finish({std::nullopt, "bangumi_unavailable"}); }
    }, [finish](std::string code) { finish({std::nullopt, std::move(code)}); });
}

void BangumiService::episodeMapping(std::int64_t id, SubjectCompletion completion, Priority priority) {
    if (id <= 0) { completion({std::nullopt, "bangumi_invalid_subject_id"}); return; }
    const auto now = clock_();
    std::optional<BangumiSubject> cached;
    {
        std::lock_guard lock(rateMutex_);
        const auto found = episodeCache_.find(id);
        if (found != episodeCache_.end() && found->second.first > now) cached = found->second.second;
    }
    if (cached) { completion({std::move(cached), ""}); return; }
    BangumiSubject subject;
    subject.id = id; subject.type = 2;
    auto self = shared_from_this();
    loadEpisodes(std::move(subject), 0, -1, [self, id, completion = std::move(completion)](BangumiSubjectResult result) {
        if (result.subject) {
            // 完整分集表短期复用；失败仅退避一分钟，避免高频扫描占满网络限额。
            std::lock_guard lock(self->rateMutex_);
            if (self->episodeCache_.size() >= 256) self->episodeCache_.clear();
            const auto ttl = std::chrono::minutes{result.subject->episodes.empty() ? 1 : 15};
            self->episodeCache_[id] = {self->clock_() + ttl, *result.subject};
        }
        completion(std::move(result));
    }, 0, priority);
}

void BangumiService::loadEpisodes(BangumiSubject subject, int offset, int total, SubjectCompletion completion, int pages, Priority priority) {
    auto finish = once<BangumiSubjectResult>(std::move(completion));
    auto fallback = [finish, subject]() mutable { subject.episodes.clear(); finish({std::move(subject), ""}); };
    if (pages >= 10) { fallback(); return; }
    auto self = shared_from_this();
    scheduleRequest(priority, [self, subject = std::move(subject), offset, total, finish, fallback, pages, priority]() mutable {
    try {
        const auto id = subject.id;
        self->transport_.episodes(id, offset,
            [self, subject = std::move(subject), offset, total, finish, fallback, pages, priority]
            (std::optional<BangumiTransport::Response> response, std::string) mutable {
            // 分集表不可用时仍允许绑定；不以猜测的偏移量改写集数。
            if (response && response->status == 429) self->remoteRateLimited();
            if (!response || response->status != 200) { fallback(); return; }
            try {
                if (response->body.empty() || response->body.size() > kMaxBody) throw std::invalid_argument("episode size");
                const auto document = nlohmann::json::parse(response->body);
                if (!document.is_object() || !document.contains("total") || !document["total"].is_number_integer() ||
                    !document.contains("offset") || !document["offset"].is_number_integer() || document["offset"].get<int>() != offset ||
                    !document.contains("data") || !document["data"].is_array() || document["data"].size() > 100)
                    throw std::invalid_argument("episode page");
                const auto count = document["total"].get<int>();
                if (count < 0 || count > 1000 || (total >= 0 && total != count) ||
                    offset + static_cast<int>(document["data"].size()) > count)
                    throw std::invalid_argument("episode total");
                std::unordered_set<std::string> sorts, eps;
                for (const auto& mapping : subject.episodes) { sorts.insert(mapping.sort); eps.insert(mapping.ep); }
                for (const auto& item : document["data"]) {
                    if (!item.is_object() || !item.contains("type") || !item["type"].is_number_integer() || item["type"].get<int>() != 0 ||
                        !item.contains("sort") || !item["sort"].is_number() || !item.contains("ep") || !item["ep"].is_number())
                        throw std::invalid_argument("episode fields");
                    const double sort = item["sort"].get<double>(), ep = item["ep"].get<double>();
                    if (!std::isfinite(sort) || !std::isfinite(ep) || sort <= 0 || ep <= 0 || sort > 10000 || ep > 10000 ||
                        std::floor(sort) != sort || std::floor(ep) != ep) throw std::invalid_argument("episode number");
                    BangumiEpisodeMapping mapping{std::to_string(static_cast<int>(sort)), std::to_string(static_cast<int>(ep))};
                    if (!sorts.insert(mapping.sort).second || !eps.insert(mapping.ep).second) throw std::invalid_argument("episode ambiguity");
                    subject.episodes.push_back(std::move(mapping));
                }
                const auto next = offset + static_cast<int>(document["data"].size());
                if (next < count) {
                    if (next == offset) throw std::invalid_argument("empty episode page");
                    self->loadEpisodes(std::move(subject), next, count, finish, pages + 1, priority);
                } else finish({std::move(subject), ""});
            } catch (...) { fallback(); }
        });
    } catch (...) { fallback(); }
    }, [fallback](std::string) mutable { fallback(); });
}

void BangumiService::aliasSubject(std::string title, SubjectCompletion completion, Priority priority) {
    try { (void)normalizeQuery(title); }
    catch (...) { completion({std::nullopt, "bangumi_invalid_query"}); return; }
    auto self = shared_from_this();
    auto finish = once<BangumiSubjectResult>(std::move(completion));
    auto attempt = std::make_shared<std::function<void(std::string, bool)>>();
    *attempt = [self, title, finish, priority, weak = std::weak_ptr<std::function<void(std::string, bool)>>(attempt)]
        (std::string keyword, bool retry) {
        const auto next = weak.lock();
        self->scheduleRequest(priority, [self, keyword, next, title, finish, retry, priority] {
        try {
            self->transport_.searchAliases(keyword,
                [self, title, finish, next, retry, priority](std::optional<BangumiTransport::Response> response, std::string) {
                if (response && response->status == 429) { self->remoteRateLimited(); finish({std::nullopt, "bangumi_remote_rate_limited"}); return; }
            if (!response || retryable(response->status)) {
                    finish({std::nullopt, "bangumi_unavailable"}); return;
                }
                if (response->status != 200) { finish({std::nullopt, "bangumi_http_error"}); return; }
                std::optional<std::int64_t> id;
                try { id = uniqueLegacyId(response->body); }
                catch (...) { finish({std::nullopt, "bangumi_bad_response"}); return; }
                if (id && *id < 0) { finish({std::nullopt, ""}); return; }
                if (!id) {
                    const auto prefix = retry ? distinctivePrefix(title) : std::string{};
                    if (!prefix.empty() && prefix != title) {
                        if (next) { (*next)(prefix, false); return; }
                    }
                    finish({std::nullopt, ""}); return;
                }
                self->subject(*id, [title, finish](BangumiSubjectResult result) {
                    if (result.subject && !verifiedAlias(title, *result.subject)) result.subject.reset();
                    finish(std::move(result));
                }, priority);
            });
        } catch (...) { finish({std::nullopt, "bangumi_unavailable"}); }
        }, [finish](std::string code) { finish({std::nullopt, std::move(code)}); });
    };
    (*attempt)(std::move(title), true);
}

} // namespace anime_vault
