#include "anime_vault/infrastructure/network/DrogonCoverTransport.hpp"
#include "anime_vault/infrastructure/network/WindowsSystemHttp.hpp"

namespace anime_vault {
DrogonCoverTransport::DrogonCoverTransport(std::string userAgent)
    : userAgent_(std::move(userAgent)),
      client_(drogon::HttpClient::newHttpClient("https://lain.bgm.tv", nullptr, false, true)) {}

void DrogonCoverTransport::fetch(std::string url, Completion completion) {
    const auto path = CoverScraper::allowedImagePath(url);
    if (!path) { completion(std::nullopt, "cover_invalid_url"); return; }
#ifdef _WIN32
    windowsSystemHttpsRequest("lain.bgm.tv", *path, "GET", userAgent_, "", 5 * 1024 * 1024,
        [completion = std::move(completion)](WindowsHttpResult result) mutable {
            if (!result.body || result.status != 200) {
                completion(std::nullopt, "cover_unavailable"); return;
            }
            completion(std::move(result.body), "");
        });
#else
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get);
    request->setPath(*path);
    request->addHeader("User-Agent", userAgent_);
    // The host is fixed and TLS verified; redirects are rejected rather than
    // following a remote URL supplied by a third-party metadata response.
    client_->sendRequest(request, [completion = std::move(completion)](
        drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (result != drogon::ReqResult::Ok || !response ||
            response->statusCode() != drogon::k200OK ||
            response->getBody().size() > 5 * 1024 * 1024) {
            completion(std::nullopt, "cover_unavailable"); return;
        }
        completion(std::string(response->getBody()), "");
    }, 8.0);
#endif
}
}
