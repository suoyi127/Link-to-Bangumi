#pragma once
#include "anime_vault/services/NovelService.hpp"
#include "anime_vault/ports/BangumiTransport.hpp"
#include <map>
#include <mutex>
#include <deque>

namespace anime_vault {
struct NovelSubject {
    std::int64_t id{};
    std::string name, nameCn, author, summary, coverUrl, platform;
    std::vector<std::string> aliases;
    bool series{};
};
struct NovelSearchResult { std::vector<NovelSubject> items; std::string errorCode; };
struct NovelScrapeResult { std::string errorCode; bool bound{}, coverUpdated{}; };
class NovelBangumiService : public std::enable_shared_from_this<NovelBangumiService> {
public:
    NovelBangumiService(NovelService& novels, BangumiTransport& transport, CoverImageFetcher& covers)
        : novels_(novels), transport_(transport), covers_(covers) {}
    using SearchCompletion = std::function<void(NovelSearchResult)>;
    using Completion = std::function<void(NovelScrapeResult)>;
    void search(std::string query, SearchCompletion completion);
    void scrape(std::int64_t id, bool file, std::optional<std::int64_t> subjectId, Completion completion);
    std::size_t queue(const std::vector<std::int64_t>& workIds);
    static NovelSubject parseSubject(const std::string& body);
    static std::optional<std::int64_t> uniqueMatch(const std::string& title, const std::vector<NovelSubject>& subjects);
private:
    void bindSubject(std::int64_t id, bool file, std::int64_t subjectId, bool manual, std::uint64_t generation, Completion completion);
    NovelService& novels_;
    BangumiTransport& transport_;
    CoverImageFetcher& covers_;
    std::mutex mutex_;
    std::map<std::pair<std::int64_t, bool>, std::uint64_t> generations_;
    void pump();
    std::deque<std::int64_t> pending_;
    bool pumping_{}, batchActive_{};
};
}
