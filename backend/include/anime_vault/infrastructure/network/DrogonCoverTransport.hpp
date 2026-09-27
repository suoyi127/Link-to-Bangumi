#pragma once

#include "anime_vault/services/CoverScraper.hpp"

#include <drogon/HttpClient.h>

namespace anime_vault {
class DrogonCoverTransport final : public CoverImageFetcher {
public:
    explicit DrogonCoverTransport(std::string userAgent);
    void fetch(std::string url, Completion completion) override;
private:
    std::string userAgent_;
    drogon::HttpClientPtr client_;
};
}
