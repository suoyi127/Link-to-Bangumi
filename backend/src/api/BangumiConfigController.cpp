#include "anime_vault/api/BangumiConfigController.hpp"
#include "anime_vault/infrastructure/network/DrogonBangumiTransport.hpp"

#include <drogon/drogon.h>
#include <atomic>
#include <functional>

namespace anime_vault::api {
namespace {
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
using Request = drogon::HttpRequestPtr;
std::atomic_uint64_t sequence{0};
void reply(Callback callback, int status, Json::Value payload) {
    const auto id = std::to_string(++sequence);
    payload["requestId"] = id;
    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status));
    response->addHeader("X-Request-Id", id);
    callback(response);
}
void failure(Callback callback, int status, const std::string& code) {
    Json::Value payload;
    payload["error"]["code"] = code;
    payload["error"]["message"] = code;
    reply(std::move(callback), status, std::move(payload));
}
Json::Value summaryJson(const BangumiConfigSummary& summary) {
    Json::Value value;
    value["userAgent"] = summary.userAgent;
    value["source"] = summary.source;
    value["configured"] = summary.configured;
    return value;
}
std::string readAgent(const drogon::HttpRequestPtr& request) {
    if (request->body().size() > 1024) throw BangumiConfigError("request_too_large", "request too large");
    if (!request->getParameters().empty()) throw BangumiConfigError("invalid_request", "query not allowed");
    const auto body = request->getJsonObject();
    if (!body) throw BangumiConfigError("invalid_request", "JSON object required");
    return parseBangumiUserAgent(*body);
}
void handleError(Callback callback, const std::exception& error) {
    if (const auto* known = dynamic_cast<const BangumiConfigError*>(&error)) {
        const int status = known->code == "request_too_large" ? 413 :
            known->code == "bangumi_config_storage_failed" || known->code == "bangumi_config_corrupt" ? 500 : 400;
        failure(std::move(callback), status, known->code);
    } else failure(std::move(callback), 500, "internal_error");
}
}

std::string parseBangumiUserAgent(const Json::Value& body) {
    if (!body.isObject() || body.size() != 1 || !body["userAgent"].isString())
        throw BangumiConfigError("invalid_request", "userAgent required");
    auto agent = body["userAgent"].asString();
    if (!BangumiConnectionManager::validUserAgent(agent))
        throw BangumiConfigError("invalid_user_agent", "invalid Bangumi User-Agent");
    return agent;
}

void registerBangumiConfigEndpoints(std::shared_ptr<BangumiConnectionManager> manager) {
    drogon::app().registerHandler("/api/bangumi/config", [manager](const Request& request, Callback&& callback) {
        if (!request->body().empty() || !request->getParameters().empty()) {
            failure(std::move(callback), 400, "invalid_request"); return;
        }
        reply(std::move(callback), 200, summaryJson(manager->summary()));
    }, {drogon::Get});
    drogon::app().registerHandler("/api/bangumi/config", [manager](const Request& request, Callback&& callback) {
        try { manager->save(readAgent(request)); reply(std::move(callback), 200, summaryJson(manager->summary())); }
        catch (const std::exception& error) { handleError(std::move(callback), error); }
    }, {drogon::Put});
    drogon::app().registerHandler("/api/bangumi/config", [manager](const Request& request, Callback&& callback) {
        if (!request->body().empty() || !request->getParameters().empty()) {
            failure(std::move(callback), 400, "invalid_request"); return;
        }
        try { manager->clear(); reply(std::move(callback), 200, summaryJson(manager->summary())); }
        catch (const std::exception& error) { handleError(std::move(callback), error); }
    }, {drogon::Delete});
    drogon::app().registerHandler("/api/bangumi/config/test", [](const Request& request, Callback&& callback) {
        try {
            const auto agent = readAgent(request);
            // Bypass cached search results: this request checks the proposed identity upstream.
            auto transport = std::make_shared<DrogonBangumiTransport>(agent);
            transport->subject(1, [transport, callback = std::move(callback)](auto response, auto error) mutable {
                Json::Value result;
                result["connected"] = response && response->status == 200;
                result["errorCode"] = !error.empty() ? error :
                    (!response ? "network_error" : response->status == 200 ? "" : "bangumi_http_error");
                reply(std::move(callback), 200, std::move(result));
            });
        } catch (const std::exception& error) { handleError(std::move(callback), error); }
    }, {drogon::Post});
}
}
