#include "anime_vault/api/GameController.hpp"
#include "anime_vault/api/LocalRequest.hpp"
#include <drogon/drogon.h>
#include <atomic>
#include <set>
namespace anime_vault::api {
namespace {
using Request = drogon::HttpRequestPtr;
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
struct GameImportRequest { std::string path; };
struct GameEditRequest { std::string title, developer, summary, path; };
struct GameConfirmRequest { bool confirmed{}; };
struct GameScrapeRequest { std::optional<std::int64_t> subjectId; };
struct GameVndbRequest { std::optional<std::string> vndbId; };
std::atomic_uint64_t sequence{0};
void reply(Callback callback, Json::Value payload, int status = 200) {
    const auto id = "game-" + std::to_string(++sequence); payload["requestId"] = id;
    auto response = drogon::HttpResponse::newHttpJsonResponse(payload);
    response->setStatusCode(static_cast<drogon::HttpStatusCode>(status)); response->addHeader("X-Request-Id", id); callback(response);
}
void failure(Callback callback, const std::string& code) {
    const int status = code == "game_storage_error" ? 500 : code == "game_not_found" ? 404 : code == "cross_site_request_forbidden" ? 403 : code == "request_too_large" ? 413 : code == "game_is_accessible" || code == "game_path_already_imported" ? 409 : 400;
    Json::Value payload; payload["error"]["code"] = code; payload["error"]["message"] = code; reply(std::move(callback), payload, status);
}
void error(Callback callback, const std::exception& error) {
    const auto* known = dynamic_cast<const GameError*>(&error); failure(std::move(callback), known ? known->code : "game_storage_error");
}
const Json::Value& body(const Request& request, const std::set<std::string>& fields) {
    // 运行 EXE 与记录修改仅接受本机前端请求，确认字段由明确的用户操作提交。
    if (!localRequestAllowed(request)) throw GameError("cross_site_request_forbidden");
    if (request->body().size() > 28000) throw GameError("request_too_large");
    const auto json = request->getJsonObject(); if (!json || !json->isObject()) throw GameError("invalid_request");
    for (const auto& name : json->getMemberNames()) if (!fields.contains(name)) throw GameError("invalid_request");
    return *json;
}
std::string stringField(const Json::Value& json, const char* key) {
    if (!json[key].isString()) throw GameError("invalid_request"); return json[key].asString();
}
GameConfirmRequest confirmation(const Request& request) {
    const auto& json = body(request, {"confirmed"});
    if (!json["confirmed"].isBool()) throw GameError("invalid_request");
    GameConfirmRequest result{json["confirmed"].asBool()};
    if (!result.confirmed) throw GameError("confirmation_required"); return result;
}
Json::Value gameJson(const GameResource& game) {
    Json::Value out; out["id"] = Json::Int64(game.id); out["title"] = game.title; out["path"] = game.path; out["developer"] = game.developer; out["summary"] = game.summary; out["platform"] = game.platform;
    out["subjectId"] = game.subjectId ? Json::Value(Json::Int64(*game.subjectId)) : Json::Value();
    out["manualMetadata"] = game.manualMetadata; out["missing"] = game.missing; out["coverUrl"] = game.hasCover ? "/api/games/" + std::to_string(game.id) + "/cover" : "";
    out["vndbId"] = game.vndbId; out["metadataSource"] = game.metadataSource; return out;
}
Json::Value scrapeJson(const GameScrapeResult& result) {
    Json::Value out; out["bound"] = result.bound; out["coverUpdated"] = result.coverUpdated; out["errorCode"] = result.errorCode; out["source"] = result.source; return out;
}
}
void registerGameEndpoints(std::shared_ptr<GameService> games, std::shared_ptr<GameBangumiService> bangumi, ProcessLauncher& launcher) {
    drogon::app().registerHandler("/api/game-vndb/search", [bangumi](const Request& request, Callback&& callback) {
        bangumi->searchVndb(request->getParameter("q"), [callback = std::move(callback)](VndbSearchResult result) mutable {
            if (!result.errorCode.empty()) { failure(std::move(callback), result.errorCode); return; }
            Json::Value out; out["items"] = Json::Value(Json::arrayValue); out["more"] = result.more;
            for (const auto& subject : result.items) { Json::Value item; item["id"] = subject.id; item["name"] = subject.name; item["nameCn"] = subject.nameCn; item["platform"] = subject.platform; out["items"].append(item); }
            reply(std::move(callback), out);
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/games/{1}/vndb/scrape", [bangumi](const Request& request, Callback&& callback, std::int64_t id) {
        try {
            const auto& json = body(request, {"vndbId"}); GameVndbRequest input;
            if (json.isMember("vndbId")) input.vndbId = stringField(json, "vndbId");
            bangumi->scrapeVndb(id, input.vndbId, [callback = std::move(callback)](GameScrapeResult result) mutable { reply(std::move(callback), scrapeJson(result)); });
        } catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Post});
    drogon::app().registerHandler("/api/games", [games](const Request&, Callback&& callback) {
        try { Json::Value out; out["items"] = Json::Value(Json::arrayValue); for (const auto& game : games->list()) out["items"].append(gameJson(game)); reply(std::move(callback), out); }
        catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Get});
    drogon::app().registerHandler("/api/game-imports", [games, bangumi](const Request& request, Callback&& callback) {
        try {
            const GameImportRequest input{stringField(body(request, {"path"}), "path")};
            const auto game = games->importExecutable(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(input.path.data()), input.path.size())));
            bangumi->scrape(game.id, {}, [games, id = game.id, callback = std::move(callback)](GameScrapeResult result) mutable {
                try { auto out = scrapeJson(result); out["game"] = gameJson(games->get(id)); reply(std::move(callback), out, 201); }
                catch (const std::exception& cause) { error(std::move(callback), cause); }
            });
        } catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Post});
    drogon::app().registerHandler("/api/games/{1}", [games](const Request& request, Callback&& callback, std::int64_t id) {
        try { const auto& json = body(request, {"title", "developer", "summary", "path"});
            const GameEditRequest input{stringField(json, "title"), stringField(json, "developer"), stringField(json, "summary"), stringField(json, "path")};
            games->edit(id, input.title, input.developer, input.summary, input.path); reply(std::move(callback), gameJson(games->get(id)));
        } catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Put});
    drogon::app().registerHandler("/api/games/{1}", [games](const Request& request, Callback&& callback, std::int64_t id) {
        try { confirmation(request); games->removeUnavailable(id); Json::Value out; out["removedId"] = Json::Int64(id); reply(std::move(callback), out); }
        catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Delete});
    drogon::app().registerHandler("/api/games/{1}/launch", [games, &launcher](const Request& request, Callback&& callback, std::int64_t id) {
        try { confirmation(request); games->launch(id, launcher); Json::Value out; out["started"] = true; reply(std::move(callback), out, 202); }
        catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Post});
    drogon::app().registerHandler("/api/games/{1}/scrape", [bangumi](const Request& request, Callback&& callback, std::int64_t id) {
        try { const auto& json = body(request, {"subjectId"}); GameScrapeRequest input;
            if (json.isMember("subjectId")) { if (!json["subjectId"].isInt64() || json["subjectId"].asInt64() <= 0) throw GameError("invalid_request"); input.subjectId = json["subjectId"].asInt64(); }
            bangumi->scrape(id, input.subjectId, [callback = std::move(callback)](GameScrapeResult result) mutable { reply(std::move(callback), scrapeJson(result)); });
        } catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Post});
    drogon::app().registerHandler("/api/game-bangumi/search", [bangumi](const Request& request, Callback&& callback) {
        bangumi->search(request->getParameter("q"), [callback = std::move(callback)](GameSearchResult result) mutable {
            if (!result.errorCode.empty()) { failure(std::move(callback), result.errorCode); return; }
            Json::Value out; out["items"] = Json::Value(Json::arrayValue);
            for (const auto& subject : result.items) { Json::Value item; item["id"] = Json::Int64(subject.id); item["name"] = subject.name; item["nameCn"] = subject.nameCn; item["platform"] = subject.platform; out["items"].append(item); }
            reply(std::move(callback), out);
        });
    }, {drogon::Get});
    drogon::app().registerHandler("/api/games/{1}/cover", [games](const Request&, Callback&& callback, std::int64_t id) {
        try { const auto cover = games->cover(id); if (!cover) { failure(std::move(callback), "cover_unavailable"); return; }
            auto response = drogon::HttpResponse::newHttpResponse(); response->setContentTypeString(cover->mimeType); response->setBody(cover->bytes); response->addHeader("Cache-Control", "no-cache"); response->addHeader("X-Content-Type-Options", "nosniff"); callback(response);
        } catch (const std::exception& cause) { error(std::move(callback), cause); }
    }, {drogon::Get});
}
}
