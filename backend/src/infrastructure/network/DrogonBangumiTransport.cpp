#include "anime_vault/infrastructure/network/DrogonBangumiTransport.hpp"
#include "anime_vault/infrastructure/network/WindowsSystemHttp.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>

namespace anime_vault {
namespace {
constexpr std::size_t kMaxBody = 512 * 1024;
bool validAgent(const std::string& value) {
    return !value.empty() && value.size() <= 200 &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch >= 0x20 && ch != 0x7f; });
}
std::string encodePathSegment(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            encoded.push_back(static_cast<char>(ch));
        else {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 15]);
        }
    }
    return encoded;
}
}

DrogonBangumiTransport::DrogonBangumiTransport(std::string userAgent)
    : userAgent_(std::move(userAgent)),
      client_(drogon::HttpClient::newHttpClient("https://api.bgm.tv", nullptr, false, true)) {
    if (!validAgent(userAgent_)) throw std::invalid_argument("Bangumi User-Agent is required");
}

void DrogonBangumiTransport::send(const drogon::HttpRequestPtr& request, Completion completion) {
    request->addHeader("User-Agent", userAgent_);
#ifdef _WIN32
    windowsSystemHttpsRequest("api.bgm.tv", request->path(),
        request->method() == drogon::Post ? "POST" : "GET", userAgent_,
        std::string(request->getBody()), kMaxBody,
        [completion = std::move(completion)](WindowsHttpResult result) mutable {
            if (!result.body) { completion(std::nullopt, "network_error"); return; }
            completion(Response{result.status, std::move(*result.body)}, "");
        });
#else
    // The client targets api.bgm.tv directly and verifies the TLS certificate.
    client_->sendRequest(request,
        [completion = std::move(completion)](drogon::ReqResult result,
                                             const drogon::HttpResponsePtr& response) mutable {
            if (result != drogon::ReqResult::Ok || !response) {
                completion(std::nullopt, "network_error");
                return;
            }
            const auto body = response->getBody();
            if (body.size() > kMaxBody) {
                // Empty body is rejected by the service parser without copying remote content.
                completion(Response{static_cast<int>(response->statusCode()), ""}, "");
                return;
            }
            completion(Response{static_cast<int>(response->statusCode()), std::string(body)}, "");
        }, 5.0);
#endif
}

void DrogonBangumiTransport::search(std::string keyword, Completion completion) {
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Post);
    request->setPath("/v0/search/subjects?limit=20");
    request->setPathEncode(false);
    request->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    request->setBody(nlohmann::json{{"keyword", std::move(keyword)},
                                     {"filter", {{"type", {2}}}}}.dump());
    send(request, std::move(completion));
}

void DrogonBangumiTransport::searchAliases(std::string keyword, Completion completion) {
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get);
    request->setPath("/search/subject/" + encodePathSegment(keyword) + "?type=2&responseGroup=small");
    request->setPathEncode(false);
    send(request, std::move(completion));
}

void DrogonBangumiTransport::subject(std::int64_t id, Completion completion) {
    if (id <= 0) { completion(std::nullopt, "invalid_subject_id"); return; }
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Get);
    request->setPath("/v0/subjects/" + std::to_string(id));
    send(request, std::move(completion));
}

} // namespace anime_vault
