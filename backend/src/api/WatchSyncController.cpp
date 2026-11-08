#include "anime_vault/api/WatchSyncController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/LocalRequest.hpp"
#include "anime_vault/services/WatchSyncService.hpp"

namespace anime_vault::api {
namespace {
Json::Value summary(const WatchSyncStatus& status) {
    Json::Value result;
    result["enabled"] = status.enabled;
    result["connected"] = status.connected;
    result["username"] = status.username;
    result["pending"] = Json::Int64(status.pending);
    result["errorCode"] = status.errorCode;
    result["issues"] = Json::arrayValue;
    for (const auto& issue : status.issues) {
        Json::Value row;
        row["mediaId"] = Json::Int64(issue.mediaId);
        row["errorCode"] = issue.errorCode;
        row["title"] = issue.title;
        row["episodeNumber"] = issue.episodeNumber;
        row["episodeType"] = issue.episodeType;
        result["issues"].append(std::move(row));
    }
    return result;
}
Json::Value body(const Request& req) {
    if (!req->getParameters().empty())
        throw ApiError(400, "invalid_request", "query not allowed");
    if (req->body().size() > 4096)
        throw ApiError(413, "request_too_large", "request too large");
    const auto json = req->getJsonObject();
    if (!json || !json->isObject())
        throw ApiError(400, "invalid_request", "object required");
    return *json;
}
void submit(const std::shared_ptr<WatchSyncService>& service, const Request& req, Callback cb,
            std::function<WatchSyncStatus()> action) {
    if (!localRequestAllowed(req)) {
        deliver(std::move(cb), requestContext(req, RequestIdPolicy::Media), 403,
                errorJson("cross_site_request_forbidden", "cross site request forbidden"));
        return;
    }
    auto task = [req, cb, action = std::move(action)] {
        respond(req, cb, [&] {
            try {
                return summary(action());
            } catch (const WatchSyncError& e) {
                throw ApiError(e.status, e.what(), e.what());
            }
        });
    };
    if (!service->post(std::move(task)))
        deliver(std::move(cb), requestContext(req, RequestIdPolicy::Media), 503,
                errorJson("sync_busy", "sync busy"));
}
} // namespace
void registerWatchSyncEndpoints(std::shared_ptr<WatchSyncService> service) {
    drogon::app().registerHandler("/api/watch-sync",
                                  [service](const Request& req, Callback&& cb) {
                                      respond(req, std::move(cb), [&] {
                                          if (!req->body().empty() || !req->getParameters().empty())
                                              throw ApiError(400, "invalid_request", "empty request required");
                                          return summary(service->status());
                                      });
                                  },
                                  {drogon::Get});
    drogon::app().registerHandler(
        "/api/watch-sync",
        [service](const Request& req, Callback&& cb) {
            submit(service, req, std::move(cb), [service, req] {
                const auto data = body(req);
                if (!hasOnlyFields(data, {"enabled", "token"}) || !data["enabled"].isBool() ||
                    (data.isMember("token") && !data["token"].isString()))
                    throw ApiError(400, "invalid_request", "invalid settings");
                return service->configure({data["enabled"].asBool(), readStringField(data, "token")});
            });
        },
        {drogon::Put});
    drogon::app().registerHandler("/api/watch-sync/collect",
                                  [service](const Request& req, Callback&& cb) {
                                      submit(service, req, std::move(cb), [service, req] {
                                          const auto data = body(req);
                                          const auto media = readPositiveIdField(data, "mediaId");
                                          if (!hasExactFields(data, {"mediaId"}) || !media)
                                              throw ApiError(400, "invalid_request", "mediaId required");
                                          return service->collect(*media);
                                      });
                                  },
                                  {drogon::Post});
}
} // namespace anime_vault::api
