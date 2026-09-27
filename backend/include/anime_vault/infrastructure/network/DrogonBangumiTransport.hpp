#pragma once

#include "anime_vault/ports/BangumiTransport.hpp"
#include <drogon/HttpClient.h>

namespace anime_vault {

class DrogonBangumiTransport final : public BangumiTransport {
public:
    // Empty or unsafe identity is rejected; callers supply their own identifiable app/developer UA.
    explicit DrogonBangumiTransport(std::string userAgent);
    void search(std::string keyword, Completion completion) override;
    void subject(std::int64_t id, Completion completion) override;
private:
    void send(const drogon::HttpRequestPtr& request, Completion completion);
    std::string userAgent_;
    drogon::HttpClientPtr client_;
};

} // namespace anime_vault
