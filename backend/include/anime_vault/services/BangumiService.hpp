#pragma once

#include "anime_vault/ports/BangumiTransport.hpp"
#include "anime_vault/services/BangumiMatcher.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>

namespace anime_vault {
class MediaRepository;

struct BangumiSearchResult {
    RankedCandidates candidates;
    std::string errorCode;
    int retryCount{};
    std::optional<std::chrono::system_clock::time_point> retryAfter;
    bool fromCache{};
    struct LocalMatch {
        std::int64_t id{};
        std::string displayTitle;
        std::optional<std::int64_t> bangumiSubjectId;
    };
    std::optional<LocalMatch> localMatch;
};

struct BangumiSubjectResult {
    std::optional<BangumiSubject> subject;
    std::string errorCode;
};

// 封装 Bangumi 查询、缓存与限流策略；真实网络传输由独立端口注入。
class BangumiService final : public std::enable_shared_from_this<BangumiService> {
public:
    enum class Priority { manual, background };
    using Scheduler = std::function<void(std::chrono::milliseconds, std::function<void()>)>;
    using Clock = std::function<std::chrono::system_clock::time_point()>;
    using SearchCompletion = std::function<void(BangumiSearchResult)>;
    using SubjectCompletion = std::function<void(BangumiSubjectResult)>;
    BangumiService(MediaRepository& repository, BangumiTransport& transport,
                   Clock clock = [] { return std::chrono::system_clock::now(); }, Scheduler scheduler = {});
    void search(BangumiMatchQuery query, SearchCompletion completion, Priority priority = Priority::manual);
    void aliasSubject(std::string title, SubjectCompletion completion, Priority priority = Priority::manual);
    void subject(std::int64_t id, SubjectCompletion completion, Priority priority = Priority::manual);
    void episodeMapping(std::int64_t id, SubjectCompletion completion, Priority priority = Priority::manual);
private:
    void loadEpisodes(BangumiSubject subject, int offset, int total, SubjectCompletion completion, int pages = 0, Priority priority = Priority::manual);
    bool allowRequest(std::chrono::system_clock::time_point now);
    void scheduleRequest(Priority priority, std::function<void()> action, std::function<void(std::string)> rejected);
    void pumpRequests();
    void remoteRateLimited();
    struct QueuedRequest { std::function<void()> action; };
    Scheduler scheduler_;
    std::deque<QueuedRequest> manualQueue_, backgroundQueue_;
    bool pumping_{}, timerArmed_{};
    std::chrono::system_clock::time_point remoteRetryAt_{};
    std::unordered_map<std::int64_t, std::pair<std::chrono::system_clock::time_point, BangumiSubject>> subjectCache_;
    MediaRepository& repository_;
    BangumiTransport& transport_;
    Clock clock_;
    std::mutex rateMutex_;
    std::vector<std::chrono::system_clock::time_point> recentRequests_;
    std::unordered_map<std::int64_t, std::pair<std::chrono::system_clock::time_point, BangumiSubject>> episodeCache_;
};

} // namespace anime_vault
