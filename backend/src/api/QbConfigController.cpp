#include "anime_vault/api/QbConfigController.hpp"

#include <drogon/drogon.h>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <utility>

namespace anime_vault::api {
namespace {
using Request = drogon::HttpRequestPtr;
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
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
QbConfigRequest requestBody(const Request& request) {
    if (request->body().size() > 2048)
        throw QbConfigError("request_too_large", "qB request too large");
    if (!request->getParameters().empty())
        throw QbConfigError("invalid_request", "query not allowed");
    const auto body = request->getJsonObject();
    if (!body) throw QbConfigError("invalid_request", "JSON object required");
    return parseQbConfigRequest(*body);
}
void handleError(Callback callback, const std::exception& error) {
    if (const auto* known = dynamic_cast<const QbConfigError*>(&error)) {
        const int status = known->code == "request_too_large" ? 413 :
            known->code == "qb_config_storage_failed" || known->code == "qb_config_corrupt" ||
            known->code == "qb_config_storage_unavailable" ? 500 : 400;
        failure(std::move(callback), status, known->code);
    } else failure(std::move(callback), 500, "internal_error");
}
Json::Value statusJson(const QbStatus& status) {
    Json::Value result;
    result["configured"] = status.configured;
    result["connected"] = status.connected;
    result["errorCode"] = status.errorCode;
    result["version"] = status.version;
    result["torrentCount"] = status.torrentCount;
    result["completedCount"] = status.completedCount;
    return result;
}
}

QbConfigRequest parseQbConfigRequest(const Json::Value& body) {
    if (!body.isObject() || body.size() != 3 || !body["url"].isString() ||
        !body["username"].isString() || !body["password"].isString())
        throw QbConfigError("invalid_request", "url, username and password required");
    return {body["url"].asString(), body["username"].asString(), body["password"].asString()};
}

Json::Value qbConfigJson(const QbConfigSummary& summary) {
    Json::Value result;
    result["url"] = summary.url;
    result["username"] = summary.username;
    result["source"] = summary.source;
    result["configured"] = summary.configured;
    return result;
}

void registerQbConfigEndpoints(std::shared_ptr<QbConnectionManager> manager) {
    drogon::app().registerHandler("/api/qb/config", [manager](const Request& request, Callback&& callback) {
        if (!request->body().empty() || !request->getParameters().empty()) {
            failure(std::move(callback), 400, "invalid_request"); return;
        }
        reply(std::move(callback), 200, qbConfigJson(manager->summary()));
    }, {drogon::Get});
    drogon::app().registerHandler("/api/qb/config", [manager](const Request& request, Callback&& callback) {
        try {
            const auto dto = requestBody(request);
            manager->save({dto.url, dto.username, dto.password});
            reply(std::move(callback), 200, qbConfigJson(manager->summary()));
        } catch (const std::exception& error) { handleError(std::move(callback), error); }
    }, {drogon::Put});
    drogon::app().registerHandler("/api/qb/config", [manager](const Request& request, Callback&& callback) {
        if (!request->body().empty() || !request->getParameters().empty()) {
            failure(std::move(callback), 400, "invalid_request"); return;
        }
        try {
            manager->clear();
            reply(std::move(callback), 200, qbConfigJson(manager->summary()));
        } catch (const std::exception& error) { handleError(std::move(callback), error); }
    }, {drogon::Delete});
    drogon::app().registerHandler("/api/qb/config/test", [manager](const Request& request, Callback&& callback) {
        try {
            const auto dto = requestBody(request);
            auto candidate = manager->draft({dto.url, dto.username, dto.password});
            auto client = std::make_shared<QbWebClient>(candidate.url, candidate.username, candidate.password);
            client->inspect([client, callback = std::move(callback)](QbStatus status) mutable {
                reply(std::move(callback), 200, statusJson(status));
            });
        } catch (const std::exception& error) { handleError(std::move(callback), error); }
    }, {drogon::Post});
}
}
