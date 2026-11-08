#include "anime_vault/api/WatchProgressController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/LocalRequest.hpp"
#include "anime_vault/infrastructure/database/WatchProgressStore.hpp"

namespace anime_vault::api {
namespace {
Json::Value progressJson(const WatchProgress &item) {
    Json::Value row;
    row["mediaId"] = Json::Int64(item.mediaId);
    row["watched"] = item.watched;
    row["position"] = item.position;
    row["duration"] = item.duration;
    row["updatedAt"] = Json::Int64(item.updatedAt);
    return row;
}
} // namespace
void registerWatchProgressEndpoints(WatchProgressStore &store) {
    drogon::app().registerHandler("/api/watch-progress",
                                  [&store](const Request &request, Callback &&cb) {
                                      respond(request, std::move(cb), [&] {
                                          const auto params = request->getParameters();
                                          if (!request->body().empty() || params.size() != 1 ||
                                              !params.contains("animeId"))
                                              throw ApiError(400, "invalid_request", "animeId required");
                                          const auto animeId = parseId(params.at("animeId"));
                                          Json::Value out;
                                          out["items"] = Json::Value(Json::arrayValue);
                                          for (const auto &row : store.list(animeId))
                                              out["items"].append(progressJson(row));
                                          return out;
                                      });
                                  },
                                  {drogon::Get});
    drogon::app().registerHandler("/api/watch-progress/continue",
                                  [&store](const Request &request, Callback &&cb) {
                                      respond(request, std::move(cb), [&] {
                                          if (!request->body().empty() || !request->getParameters().empty())
                                              throw ApiError(400, "invalid_request", "no parameters allowed");
                                          Json::Value out;
                                          out["items"] = Json::Value(Json::arrayValue);
                                          for (const auto &item : store.continuing()) {
                                              Json::Value row;
                                              row["animeId"] = Json::Int64(item.animeId);
                                              row["mediaId"] = Json::Int64(item.mediaId);
                                              row["title"] = item.title;
                                              row["episodeNumber"] = item.episodeNumber;
                                              row["episodeType"] = item.episodeType;
                                              row["position"] = item.position;
                                              row["duration"] = item.duration;
                                              row["nextEpisode"] = item.nextEpisode;
                                              out["items"].append(row);
                                          }
                                          return out;
                                      });
                                  },
                                  {drogon::Get});
    drogon::app().registerHandler("/api/media/{1}/watched",
                                  [&store](const Request &request, Callback &&cb, std::string id) {
                                      respond(request, std::move(cb), [&] {
                                          if (!localRequestAllowed(request))
                                              throw ApiError(403, "playback_origin_forbidden", "local page required");
                                          if (!request->getParameters().empty() || request->body().size() > 128)
                                              throw ApiError(400, "invalid_request", "invalid watch request");
                                          const auto body = request->getJsonObject();
                                          if (!body || !hasExactFields(*body, {"watched"}) ||
                                              !(*body)["watched"].isBool())
                                              throw ApiError(400, "invalid_request", "watched must be a boolean");
                                          try {
                                              return progressJson(store.mark(parseId(id), (*body)["watched"].asBool()));
                                          } catch (const std::out_of_range &) {
                                              throw ApiError(404, "media_not_found", "media not found");
                                          }
                                      });
                                  },
                                  {drogon::Put});
}
} // namespace anime_vault::api
