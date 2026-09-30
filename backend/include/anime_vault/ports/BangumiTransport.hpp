#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace anime_vault {

// Bangumi 网络能力的抽象端口，便于业务服务使用真实 HTTP 或测试桩。
class BangumiTransport {
public:
    struct Response { int status{}; std::string body; };
    using Completion = std::function<void(std::optional<Response>, std::string)>;
    virtual ~BangumiTransport() = default;
    virtual void search(std::string keyword, Completion completion) = 0;
    virtual void searchAliases(std::string keyword, Completion completion) = 0;
    virtual void subject(std::int64_t id, Completion completion) = 0;
    virtual void searchBooks(std::string, Completion completion) {
        completion(std::nullopt, "bangumi_book_search_unavailable");
    }
    virtual void calendar(Completion completion) {
        completion(std::nullopt, "bangumi_calendar_unavailable");
    }
    virtual void searchGames(std::string, Completion completion) { completion(std::nullopt, "bangumi_game_search_unavailable"); }
};

} // namespace anime_vault
