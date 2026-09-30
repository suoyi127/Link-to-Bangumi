#include "anime_vault/infrastructure/network/DrogonVndbTransport.hpp"
#include "anime_vault/infrastructure/network/WindowsSystemHttp.hpp"
#include <nlohmann/json.hpp>
#include <regex>
namespace anime_vault {
DrogonVndbTransport::DrogonVndbTransport()
    : api_(drogon::HttpClient::newHttpClient("https://api.vndb.org", nullptr, false, true)),
      images_(drogon::HttpClient::newHttpClient("https://t.vndb.org", nullptr, false, true)) {}
void DrogonVndbTransport::send(bool image, std::string path, std::string body, VndbTransport::Completion completion) {
    bool permitted;
    { std::lock_guard lock(limits_->mutex); const auto now = std::chrono::steady_clock::now();
      while (!limits_->requests.empty() && now - limits_->requests.front() >= std::chrono::minutes(5)) limits_->requests.pop_front();
      permitted = limits_->active < 3 && limits_->requests.size() < 180;
      if (permitted) { ++limits_->active; limits_->requests.push_back(now); }
    }
    if (!permitted) { completion({}, "vndb_rate_limited"); return; }
    auto finish = [limits = limits_, completion = std::move(completion)](std::optional<VndbTransport::Response> response, std::string error) mutable {
        { std::lock_guard lock(limits->mutex); --limits->active; }
        completion(std::move(response), std::move(error));
    };
    const auto maximum = image ? 8 * 1024 * 1024 : 512 * 1024;
#ifdef _WIN32
    windowsSystemHttpsRequest(image ? "t.vndb.org" : "api.vndb.org", path, image ? "GET" : "POST",
        "suoyi127/Link-to-Bangumi/0.1 (Windows)", std::move(body), maximum,
        [finish = std::move(finish)](WindowsHttpResult result) mutable {
            if (!result.body) { finish({}, "vndb_network_error"); return; }
            finish(VndbTransport::Response{result.status, std::move(*result.body)}, "");
        });
#else
    auto request = drogon::HttpRequest::newHttpRequest(); request->setMethod(image ? drogon::Get : drogon::Post);
    request->setPath(path); request->setPathEncode(false); request->setContentTypeCode(drogon::CT_APPLICATION_JSON); request->setBody(std::move(body));
    request->addHeader("User-Agent", "suoyi127/Link-to-Bangumi/0.1");
    (image ? images_ : api_)->sendRequest(request, [finish = std::move(finish), maximum](drogon::ReqResult result, const drogon::HttpResponsePtr& response) mutable {
        if (result != drogon::ReqResult::Ok || !response || response->body().size() > static_cast<std::size_t>(maximum)) { finish({}, "vndb_network_error"); return; }
        finish(VndbTransport::Response{static_cast<int>(response->statusCode()), std::string(response->body())}, "");
    }, 12.0);
#endif
}
void DrogonVndbTransport::query(std::string filters, VndbTransport::Completion completion) {
    const auto body = nlohmann::json{{"filters", nlohmann::json::parse(filters)}, {"fields", "title,alttitle,titles{lang,title,latin},aliases,description,developers.name,platforms,image.url"}, {"results", 20}}.dump();
    send(false, "/kana/vn", body, std::move(completion));
}
void DrogonVndbTransport::search(std::string keyword, VndbTransport::Completion completion) {
    query(nlohmann::json::array({"search", "=", std::move(keyword)}).dump(), std::move(completion));
}
void DrogonVndbTransport::subject(std::string id, VndbTransport::Completion completion) {
    if (!std::regex_match(id, std::regex("^v[1-9][0-9]{0,9}$"))) { completion({}, "invalid_vndb_id"); return; }
    query(nlohmann::json::array({"id", "=", std::move(id)}).dump(), std::move(completion));
}
void DrogonVndbTransport::fetch(std::string url, CoverImageFetcher::Completion completion) {
    const auto path = allowedCoverPath(url);
    if (!path) { completion({}, "vndb_cover_unavailable"); return; }
    send(true, *path, {}, [completion = std::move(completion)](auto response, auto error) mutable {
        if (!error.empty() || !response || response->status != 200) { completion({}, error.empty() ? "vndb_cover_http_error" : error); return; }
        completion(std::move(response->body), "");
    });
}
}
