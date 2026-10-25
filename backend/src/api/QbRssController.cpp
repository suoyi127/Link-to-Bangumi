#include "anime_vault/api/QbRssController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include <drogon/drogon.h>

namespace anime_vault::api {
void registerQbRssEndpoints(std::shared_ptr<QbConnectionManager> qb, std::filesystem::path source,
    std::function<std::string()> qbDownloadError, std::function<void()> validateSourceReady) {
    drogon::app().registerHandler("/api/qb/status", [qb](const drogon::HttpRequestPtr&,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        qb->current()->inspect([callback = std::move(callback)](anime_vault::QbStatus status) mutable {
            Json::Value payload;
            payload["configured"] = status.configured;
            payload["connected"] = status.connected;
            payload["errorCode"] = status.errorCode;
            payload["version"] = status.version;
            payload["torrentCount"] = status.torrentCount;
            payload["completedCount"] = status.completedCount;
            deliver(std::move(callback), RequestContext{}, 200, std::move(payload));
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/qb/rss", [qb](const drogon::HttpRequestPtr&,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        qb->current()->readMikanTitles([callback = std::move(callback)](anime_vault::QbMikanCatalog catalog) mutable {
            Json::Value payload;
            payload["feedCount"] = catalog.feedCount;
            payload["articleCount"] = catalog.articleCount;
            payload["pairCount"] = Json::UInt64(catalog.pairs.size());
            payload["errorCode"] = catalog.errorCode;
            payload["feeds"] = Json::Value(Json::arrayValue);
            for (const auto& feed : catalog.feeds) {
                Json::Value item;
                item["title"] = feed.title;
                item["articleCount"] = feed.articleCount;
                item["hasError"] = feed.hasError;
                payload["feeds"].append(item);
            }
            deliver(std::move(callback), RequestContext{}, 200, std::move(payload));
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/qb/rss/feeds", [qb, source, qbDownloadError, validateSourceReady](const drogon::HttpRequestPtr& request,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        const auto body = request->body().size() <= 2048 ? request->getJsonObject() : nullptr;
        if (!body || !body->isObject() || body->size() != 1 || !(*body)["url"].isString() ||
            !anime_vault::QbWebClient::validMikanFeedUrl((*body)["url"].asString())) {
            Json::Value payload;
            payload["error"]["code"] = "invalid_mikan_feed_url";
            payload["error"]["message"] = "a Mikan HTTPS RSS URL is required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }
        if (const auto error = qbDownloadError(); !error.empty()) {
            Json::Value payload;
            payload["error"]["code"] = error;
            payload["error"]["message"] = "restart or configure the qB download directory";
            auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(drogon::k409Conflict);
            callback(response);
            return;
        }
        try { validateSourceReady(); }
        catch (const MediaError& error) {
            deliver(std::move(callback), RequestContext{}, mediaErrorStatus(error.kind), errorJson(error.code, error.what()));
            return;
        }
        catch (const anime_vault::api::ApiError& error) {
            Json::Value payload;
            payload["error"]["code"] = error.code;
            payload["error"]["message"] = error.what();
            auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(static_cast<drogon::HttpStatusCode>(error.status));
            callback(response);
            return;
        }
        const auto path = std::filesystem::absolute(source).u8string();
        qb->current()->addMikanFeedWithAutoRule((*body)["url"].asString(),
            std::string(reinterpret_cast<const char*>(path.data()), path.size()),
            [callback = std::move(callback)](anime_vault::QbActionResult result) mutable {
                Json::Value payload;
                payload["success"] = result.success;
                payload["errorCode"] = result.errorCode;
                if (!result.success) {
                    payload["error"]["code"] = result.errorCode;
                    payload["error"]["message"] = "qB RSS feed action failed";
                }
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                if (!result.success) response->setStatusCode(
                    (result.errorCode == "qb_feed_exists_or_invalid" ||
                     result.errorCode == "qb_rule_exists") ? drogon::k409Conflict :
                    drogon::k503ServiceUnavailable);
                callback(response);
            });
    }, {drogon::Post});
    drogon::app().registerHandler("/api/qb/rss/rules", [qb, source, qbDownloadError, validateSourceReady](const drogon::HttpRequestPtr& request,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        const auto body = request->body().size() <= 4096 ? request->getJsonObject() : nullptr;
        if (!body || !body->isObject() || body->size() != 3 ||
            !(*body)["ruleName"].isString() || !(*body)["feedUrl"].isString() ||
            !(*body)["keyword"].isString()) {
            Json::Value payload;
            payload["error"]["code"] = "invalid_mikan_rule";
            payload["error"]["message"] = "ruleName, feedUrl and keyword are required";
            auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(drogon::k400BadRequest);
            callback(response);
            return;
        }
        if (const auto error = qbDownloadError(); !error.empty()) {
            Json::Value payload;
            payload["error"]["code"] = error;
            payload["error"]["message"] = "restart or configure the qB download directory";
            auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(drogon::k409Conflict);
            callback(response);
            return;
        }
        try { validateSourceReady(); }
        catch (const MediaError& error) {
            deliver(std::move(callback), RequestContext{}, mediaErrorStatus(error.kind), errorJson(error.code, error.what()));
            return;
        }
        catch (const anime_vault::api::ApiError& error) {
            Json::Value payload;
            payload["error"]["code"] = error.code;
            payload["error"]["message"] = error.what();
            auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
            response->setStatusCode(static_cast<drogon::HttpStatusCode>(error.status));
            callback(response);
            return;
        }
        const auto path = std::filesystem::absolute(source).u8string();
        anime_vault::QbMikanRuleSpec spec{
            (*body)["ruleName"].asString(), (*body)["feedUrl"].asString(),
            (*body)["keyword"].asString(),
            std::string(reinterpret_cast<const char*>(path.data()), path.size())};
        qb->current()->createMikanRule(std::move(spec),
            [callback = std::move(callback)](anime_vault::QbActionResult result) mutable {
                Json::Value payload;
                payload["success"] = result.success;
                payload["errorCode"] = result.errorCode;
                if (!result.success) {
                    payload["error"]["code"] = result.errorCode;
                    payload["error"]["message"] = "qB RSS rule action failed";
                }
                auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
                if (!result.success) response->setStatusCode(
                    result.errorCode == "invalid_mikan_rule" ? drogon::k400BadRequest :
                    result.errorCode == "qb_rule_exists" ? drogon::k409Conflict :
                    drogon::k503ServiceUnavailable);
                callback(response);
            });
    }, {drogon::Post});
}
}
