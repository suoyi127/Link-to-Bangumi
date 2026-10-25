#include "anime_vault/api/PlaybackController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/LocalRequest.hpp"
#include "anime_vault/services/PlayerCatalog.hpp"
#include <algorithm>

namespace anime_vault::api {
PlayMediaHttpRequest parsePlayMediaRequest(std::string_view rawId) {
    return {parseId(rawId)};
}

bool playbackOriginAllowed(std::string_view origin) {
    return origin == "http://127.0.0.1:5173" || origin == "http://localhost:5173" ||
        origin == "http://127.0.0.1:8848" || origin == "http://localhost:8848";
}

void registerPlaybackController(SettingsRepository& repository, PlaybackService& playback) {
    const auto playerJson = [&repository] {
        const auto preferences = repository.getUiPreferences();
        Json::Value result;
        result["selectedId"] = preferences.playerType;
        result["items"] = Json::Value(Json::arrayValue);
        for (const auto& player : discoverPlayers(preferences)) {
            Json::Value item;
            item["id"] = player.id; item["name"] = player.name;
            item["executable"] = player.executable; item["available"] = player.available;
            result["items"].append(item);
        }
        return result;
    };
    drogon::app().registerHandler("/api/players", [playerJson](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!request->body().empty() || !request->getParameters().empty())
                throw ApiError(400, "invalid_request", "players takes no body or query");
            return playerJson();
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/players", [&repository, playerJson](const Request& request, Callback&& callback) {
        respond(request, std::move(callback), [&] {
            if (!localRequestAllowed(request))
                throw ApiError(403, "playback_origin_forbidden", "players requires a local page");
            const auto body = request->getJsonObject();
            if (!request->getParameters().empty() || request->body().size() > 2048 || !body ||
                !body->isObject() || body->size() != 2 || !(*body)["playerId"].isString() || !(*body)["executable"].isString())
                throw ApiError(400, "invalid_request", "playerId and executable required");
            const auto type = (*body)["playerId"].asString();
            const auto path = normalizePlayerExecutable((*body)["executable"].asString());
            if (!validPlayerType(type) || (type == "system" && !path.empty()))
                throw ApiError(400, "invalid_player", "invalid player");
            auto preferences = repository.getUiPreferences();
            if (!path.empty() && !validPlayerExecutable(path))
                throw ApiError(400, "player_not_found", "absolute existing exe required");
            preferences.playerType = type;
            preferences.playerExecutable = path;
            if (type != "system") {
                const auto options = discoverPlayers(preferences);
                const auto found = std::find_if(options.begin(), options.end(), [&](const auto& option) { return option.id == type; });
                if (found == options.end() || !found->available)
                    throw ApiError(409, "player_not_found", "player not installed");
                preferences.playerExecutable = found->executable;
            }
            repository.putUiPreferences(preferences);
            return playerJson();
        });
    }, {drogon::Put});
    drogon::app().registerHandler("/api/media/{1}/play", [&repository, &playback](
        const Request& request, Callback&& callback, std::string rawId) {
        respond(request, std::move(callback), [&] {
            if (!request->getParameters().empty() || request->body().size() > 128)
                throw ApiError(400, "invalid_request", "invalid playback request");
            if (!localRequestAllowed(request))
                throw ApiError(403, "playback_origin_forbidden", "playback requires a local page");
            const auto dto = parsePlayMediaRequest(rawId);
            const auto preferences = repository.getUiPreferences();
            auto type = preferences.playerType;
            if (!request->body().empty()) {
                const auto body = request->getJsonObject();
                if (!body || !body->isObject() || body->size() != 1 || !(*body)["playerId"].isString())
                    throw ApiError(400, "invalid_request", "only playerId allowed");
                type = (*body)["playerId"].asString();
            }
            if (!validPlayerType(type)) throw ApiError(400, "invalid_player", "unknown player");
            const auto options = discoverPlayers(preferences);
            const auto selected = std::find_if(options.begin(), options.end(), [&](const auto& option) { return option.id == type; });
            if (selected == options.end() || !selected->available)
                throw ApiError(409, "player_not_found", "player unavailable");
            try {
                playback.play(dto.mediaId, selected->executable, type);
            } catch (const PlaybackError& error) {
                const auto& code = error.code();
                const int status = code == "invalid_media_id" ? 400 :
                    code == "media_not_found" || code == "playback_file_missing" ? 404 :
                    code == "mpv_launch_failed" || code == "player_launch_failed" ? 503 : 409;
                throw ApiError(status, code, "playback could not start");
            }
            Json::Value result;
            result["mediaId"] = Json::Int64(dto.mediaId);
            result["status"] = "started";
            result["playerId"] = type;
            result["playerName"] = selected->name;
            return result;
        }, 202);
    }, {drogon::Post});
}

}
