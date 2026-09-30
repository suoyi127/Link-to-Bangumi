#pragma once
#include "anime_vault/services/CoverScraper.hpp"
namespace anime_vault {
class VndbTransport : public CoverImageFetcher {
public:
    struct Response { int status{}; std::string body; };
    using Completion = std::function<void(std::optional<Response>, std::string)>;
    virtual void search(std::string query, Completion completion) = 0;
    virtual void subject(std::string id, Completion completion) = 0;
    static std::optional<std::string> allowedCoverPath(const std::string& url);
};
}
