#include "anime_vault/api/SteamController.hpp"
#include "anime_vault/api/HttpSupport.hpp"
#include "anime_vault/api/LocalRequest.hpp"
#include <drogon/drogon.h>
#include <set>
namespace anime_vault::api {
namespace {
using Request = drogon::HttpRequestPtr;
using Callback = std::function<void(const drogon::HttpResponsePtr&)>;
struct SteamConfigSummary { std::string steamPath, steamId; bool hasApiKey{}; };
struct SteamLoginCompleteRequest { std::string token,steamId,apiKey; bool cancelled{}; };
void reply(Callback callback, Json::Value value, int status = 200) {
    deliver(std::move(callback), requestContext({}, RequestIdPolicy::Game), status, std::move(value));
}
void failure(Callback callback, const std::string& code) {
    const auto status = code == "cross_site_request_forbidden" || code == "steam_login_invalid_token" ? 403 : code == "request_too_large" ? 413 :
        code == "steam_import_in_progress" || code == "steam_login_in_progress" ? 409 : code == "steam_config_storage_failed" || code == "game_storage_error" || code == "steam_import_failed" || code == "steam_login_failed" ? 500 : 400;
    reply(std::move(callback), errorJson(code, code), status);
}
void caught(Callback callback, const std::exception& error) {
    const auto* known = dynamic_cast<const GameError*>(&error);
    failure(std::move(callback), known ? known->code : "steam_import_failed");
}
const Json::Value& body(const Request& request, const std::set<std::string>& fields) {
    if (!localRequestAllowed(request)) throw GameError("cross_site_request_forbidden");
    if (request->body().size() > 28000) throw GameError("request_too_large");
    const auto json = request->getJsonObject();
    if (!json || !json->isObject() || !hasOnlyFields(*json, fields)) throw GameError("invalid_request");
    return *json;
}
std::string field(const Json::Value& value, const char* name) {
    const auto result = readStringField(value, name);
    if (!result) throw GameError("invalid_request");
    return *result;
}
Json::Value summary(const SteamConfig& config) {
    const SteamConfigSummary dto{config.steamPath, config.steamId, !config.apiKey.empty()};
    Json::Value value; value["steamPath"] = dto.steamPath; value["steamId"] = dto.steamId; value["hasApiKey"] = dto.hasApiKey;
    return value;
}
Json::Value resultJson(const SteamImportResult& result) {
    Json::Value value; value["imported"] = result.imported; value["updated"] = result.updated;
    value["warnings"] = Json::Value(Json::arrayValue);
    for (const auto& warning : result.warnings) value["warnings"].append(warning);
    return value;
}
}
void registerSteamEndpoints(std::shared_ptr<SteamConfiguration> config, std::shared_ptr<SteamImportService> imports,
    std::shared_ptr<SteamLoginService> login) {
    if (login) {
        drogon::app().registerHandler("/api/steam/login",[login](const Request& request,Callback&& callback) {
            try {
                body(request,{});
                const auto result=login->start(request->getLocalAddr().toPort());
                Json::Value out; out["sessionId"]=result.sessionId; out["state"]=result.state;
                reply(std::move(callback),out);
            } catch (const std::exception& error) { caught(std::move(callback),error); }
        },{drogon::Post});
        drogon::app().registerHandler("/api/steam/login",[login](const Request& request,Callback&& callback) {
            try {
                if (!localRequestAllowed(request)) throw GameError("cross_site_request_forbidden");
                const auto id=request->getParameter("sessionId");
                if (id.size()!=64 || request->getParameters().size()!=1) throw GameError("invalid_request");
                const auto result=login->status(id);
                Json::Value out; out["state"]=result.state; out["errorCode"]=result.errorCode;
                reply(std::move(callback),out);
            } catch (const std::exception& error) { caught(std::move(callback),error); }
        },{drogon::Get});
        drogon::app().registerHandler("/api/steam/login/complete",[login](const Request& request,Callback&& callback) {
            try {
                const auto& json=body(request,{"token","steamId","apiKey","cancelled"});
                SteamLoginCompleteRequest dto; dto.token=field(json,"token");
                if (json.isMember("cancelled")) {
                    if (!json["cancelled"].isBool()) throw GameError("invalid_request");
                    dto.cancelled=json["cancelled"].asBool();
                }
                if (dto.cancelled) {
                    if (json.isMember("steamId") || json.isMember("apiKey")) throw GameError("invalid_request");
                } else { dto.steamId=field(json,"steamId"); dto.apiKey=field(json,"apiKey"); }
                login->complete(dto.token,dto.steamId,dto.apiKey,dto.cancelled);
                Json::Value out; out["state"]=dto.cancelled?"cancelled":"completed";
                reply(std::move(callback),out);
            } catch (const std::exception& error) { caught(std::move(callback),error); }
        },{drogon::Post});
    }
    drogon::app().registerHandler("/api/steam/config", [config](const Request& request, Callback&& callback) {
        try {
            if (!localRequestAllowed(request)) throw GameError("cross_site_request_forbidden");
            reply(std::move(callback), summary(config->current()));
        } catch (const std::exception& error) { caught(std::move(callback), error); }
    }, {drogon::Get});
    drogon::app().registerHandler("/api/steam/config", [config, imports](const Request& request, Callback&& callback) {
        try {
            const auto& json = body(request, {"steamPath", "steamId", "apiKey"});
            SteamConfig dto{field(json, "steamPath"), field(json, "steamId"), field(json, "apiKey")};
            if (imports->busy()) throw GameError("steam_import_in_progress");
            config->save(std::move(dto));
            reply(std::move(callback), summary(config->current()));
        } catch (const std::exception& error) { caught(std::move(callback), error); }
    }, {drogon::Put});
    drogon::app().registerHandler("/api/steam/imports", [config, imports](const Request& request, Callback&& callback) {
        try {
            body(request, {});
            imports->importInstalled(config->current(), [callback = std::move(callback)](SteamImportResult result) mutable {
                if (!result.errorCode.empty()) { failure(std::move(callback), result.errorCode); return; }
                reply(std::move(callback), resultJson(result));
            });
        } catch (const std::exception& error) { caught(std::move(callback), error); }
    }, {drogon::Post});
}
}
