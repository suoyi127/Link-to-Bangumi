#pragma once
#include "anime_vault/services/GameService.hpp"
#include "anime_vault/ports/BangumiTransport.hpp"
#include "anime_vault/ports/VndbTransport.hpp"
#include <map>
#include <mutex>
namespace anime_vault {
struct GameSubject {
    std::int64_t id{};
    std::string name, nameCn, developer, summary, platform, coverUrl;
    std::vector<std::string> aliases;
};
struct GameSearchResult { std::vector<GameSubject> items; std::string errorCode; };
struct GameScrapeResult { std::string errorCode; bool bound{}, coverUpdated{}; std::string source{"bangumi"}; };
struct VndbSubject {
    std::string id, name, nameCn, developer, summary, platform, coverUrl;
    std::vector<std::string> aliases;
};
struct VndbSearchResult { std::vector<VndbSubject> items; std::string errorCode; bool more{}; };
class GameBangumiService : public std::enable_shared_from_this<GameBangumiService> {
public:
    GameBangumiService(GameService& games, BangumiTransport& transport, CoverImageFetcher& covers, VndbTransport* vndb = nullptr) : games_(games), transport_(transport), covers_(covers), vndb_(vndb) {}
    using Completion = std::function<void(GameScrapeResult)>;
    void search(std::string query, std::function<void(GameSearchResult)> completion);
    void scrape(std::int64_t id, std::optional<std::int64_t> subjectId, Completion completion);
    void searchVndb(std::string query, std::function<void(VndbSearchResult)> completion);
    void scrapeVndb(std::int64_t id, std::optional<std::string> subjectId, Completion completion);
    static VndbSubject parseVndbSubject(const std::string& body);
    static std::optional<std::string> uniqueVndbMatch(const std::string& title, const std::vector<VndbSubject>& subjects);
    static GameSubject parseSubject(const std::string& body);
    static std::optional<std::int64_t> uniqueMatch(const std::string& title, const std::vector<GameSubject>& subjects);
private:
    void bindSubject(std::int64_t id, std::int64_t subjectId, std::uint64_t generation, Completion completion);
    void scrapeVndbFor(std::int64_t id, std::optional<std::string> subjectId, std::uint64_t generation, Completion completion);
    void bindVndbSubject(std::int64_t id, std::string subjectId, std::uint64_t generation, Completion completion);
    GameService& games_;
    BangumiTransport& transport_;
    CoverImageFetcher& covers_;
    VndbTransport* vndb_{};
    std::mutex mutex_;
    std::map<std::int64_t, std::uint64_t> generations_;
};
}
