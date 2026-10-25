#include "anime_vault/api/BangumiConfigController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/infrastructure/network/DrogonBangumiTransport.hpp"

#include <drogon/drogon.h>
#include <functional>

namespace anime_vault::api {
namespace {
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
using Request = drogon::HttpRequestPtr;
void reply(Callback callback, int status, Json::Value payload) {
    deliver(std::move(callback), requestContext({}, RequestIdPolicy::BangumiConfig), status, std::move(payload));
}
void failure(Callback callback, int status, const std::string& code) {
    reply(std::move(callback), status, errorJson(code, code));
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
        const int status = bangumiConfigErrorStatus(known->code);
        failure(std::move(callback), status, known->code);
    } else failure(std::move(callback), 500, "internal_error");
}
}

std::string parseBangumiUserAgent(const Json::Value& body) {
    if (!hasExactFields(body, {"userAgent"}) || !body["userAgent"].isString())
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
