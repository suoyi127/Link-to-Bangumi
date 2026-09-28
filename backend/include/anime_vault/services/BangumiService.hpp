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
    using Clock = std::function<std::chrono::system_clock::time_point()>;
    using SearchCompletion = std::function<void(BangumiSearchResult)>;
    using SubjectCompletion = std::function<void(BangumiSubjectResult)>;
    BangumiService(MediaRepository& repository, BangumiTransport& transport,
                   Clock clock = [] { return std::chrono::system_clock::now(); });
    void search(BangumiMatchQuery query, SearchCompletion completion);
    void subject(std::int64_t id, SubjectCompletion completion);
private:
    bool allowRequest(std::chrono::system_clock::time_point now);
    MediaRepository& repository_;
    BangumiTransport& transport_;
    Clock clock_;
    std::mutex rateMutex_;
    std::vector<std::chrono::system_clock::time_point> recentRequests_;
};

} // namespace anime_vault
