#include "anime_vault/api/SteamController.hpp"
#include "anime_vault/services/SteamLoginService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace {
using namespace anime_vault;
namespace fs = std::filesystem;
Json::Value json(const drogon::HttpResponsePtr& response) {
    REQUIRE(response->getJsonObject());
    REQUIRE(response->getJsonObject()->isObject());
    return *response->getJsonObject();
}
void error(const drogon::HttpResponsePtr& response,drogon::HttpStatusCode status,const std::string& code) {
    REQUIRE(response->statusCode() == status);
    REQUIRE(json(response)["error"]["code"].asString() == code);
}
#ifdef _WIN32
struct DisposableSteam {
    fs::path root = fs::temp_directory_path() /
        ("anime-vault-steam-http-" + std::to_string(GetCurrentProcessId()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(std::random_device{}()));
    fs::path data = root / "configuration";
    std::wstring credentialTarget;
    DisposableSteam() {
        fs::create_directories(root / "steamapps/common/Offline Game");
        std::ofstream(root / "steam.exe") << "MZ disposable Steam fixture";
        std::ofstream(root / "steamapps/appmanifest_570.acf") <<
            R"("AppState" { "appid" "570" "name" "Offline Game" "StateFlags" "4" "installdir" "Offline Game" })";
        auto normalized = fs::absolute(data).lexically_normal().wstring();
        std::transform(normalized.begin(),normalized.end(),normalized.begin(),
            [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        credentialTarget = L"AnimeVault/steam/" + normalized;
    }
    ~DisposableSteam() {
        // 仅清理本测试随机目标，不读取或修改用户的 Steam 配置。
        CredDeleteW(credentialTarget.c_str(),CRED_TYPE_GENERIC,0);
        std::error_code ignored; fs::remove_all(root,ignored);
    }
};
#endif
}

TEST_CASE("Steam HTTP protects credentials and imports installed games offline") {
#ifndef _WIN32
    SKIP("Windows Credential Manager is required for saved Steam configuration");
#else
    DisposableSteam fixture;
    SqliteDatabase database(fixture.root / "games.sqlite");
    database.migrate();
    SqliteGameRepository repository(database);
    std::atomic_int networkCalls{};
    SteamClient steam([&](std::string,auto callback) {
        ++networkCalls;
        callback(0,{});
    });
    auto config = std::make_shared<SteamConfiguration>(fixture.data);
    auto imports = std::make_shared<SteamImportService>(repository,steam);
    struct LoginLauncher : ProcessLauncher {
        std::vector<std::string> args;
        bool launch(const fs::path&,const std::vector<std::string>& value) override { args=value; return true; }
    } launcher;
    std::ofstream(fixture.root/"AnimeVault.exe") << "fixture";
    auto login=std::make_shared<SteamLoginService>(launcher,fixture.root,fixture.root/"Profile",
        [config](auto id,auto key) { auto value=config->current(); value.steamId=id; value.apiKey=key; config->save(value); },
        [imports]{return imports->busy();});
    api::registerSteamEndpoints(config,imports,login);

    drogon::app().addListener("127.0.0.1",18864);
    std::thread server([] { drogon::app().run(); });
    struct Stop {
        std::thread& thread;
        ~Stop() { drogon::app().quit(); thread.join(); }
    } stop{server};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18864");
    const auto send = [&](drogon::HttpMethod method,const std::string& path,
                          const Json::Value& body = Json::Value(),bool crossSite = false) {
        INFO(method << " " << path);
        auto request = body.isNull() ? drogon::HttpRequest::newHttpRequest() :
            drogon::HttpRequest::newHttpJsonRequest(body);
        request->setMethod(method); request->setPath(path);
        if (crossSite) {
            request->addHeader("Origin","https://untrusted.example");
            request->addHeader("Sec-Fetch-Site","cross-site");
        }
        auto [result,response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response;
    };
    const auto secret = std::string(32,'a');
    const auto assertNoSecret = [&](const drogon::HttpResponsePtr& response) {
        REQUIRE_FALSE(json(response).isMember("apiKey"));
        REQUIRE(response->getBody().find(secret) == std::string_view::npos);
    };

    auto initial = send(drogon::Get,"/api/steam/config");
    REQUIRE(initial->statusCode() == drogon::k200OK);
    REQUIRE_FALSE(json(initial)["hasApiKey"].asBool());
    assertNoSecret(initial);
    Json::Value body(Json::objectValue);
    const auto pathBytes = fixture.root.u8string();
    body["steamPath"] = std::string(pathBytes.begin(),pathBytes.end());
    body["steamId"] = "";
    body["apiKey"] = secret;

    for (const auto* name : {"steamPath","steamId","apiKey"}) {
        auto wrong = body; wrong[name] = 123;
        auto response = send(drogon::Put,"/api/steam/config",wrong);
        error(response,drogon::k400BadRequest,"invalid_request");
        assertNoSecret(response);
    }
    auto unknown = body; unknown["unexpected"] = true;
    error(send(drogon::Put,"/api/steam/config",unknown),drogon::k400BadRequest,"invalid_request");
    error(send(drogon::Put,"/api/steam/config",body,true),drogon::k403Forbidden,"cross_site_request_forbidden");
    REQUIRE(config->current().apiKey.empty());

    auto saved = send(drogon::Put,"/api/steam/config",body);
    REQUIRE(saved->statusCode() == drogon::k200OK);
    REQUIRE(json(saved)["hasApiKey"].asBool());
    assertNoSecret(saved);
    auto summary = send(drogon::Get,"/api/steam/config");
    REQUIRE(summary->statusCode() == drogon::k200OK);
    REQUIRE(json(summary)["hasApiKey"].asBool());
    REQUIRE(json(summary)["steamPath"] == body["steamPath"]);
    assertNoSecret(summary);
    error(send(drogon::Get,"/api/steam/config",Json::Value(),true),
        drogon::k403Forbidden,"cross_site_request_forbidden");
    body["apiKey"] = "";
    auto retained = send(drogon::Put,"/api/steam/config",body);
    REQUIRE(retained->statusCode() == drogon::k200OK);
    assertNoSecret(retained);
    REQUIRE(config->current().apiKey == secret);
    REQUIRE(SteamConfiguration(fixture.data).current().apiKey == secret);

    const Json::Value empty(Json::objectValue);
    error(send(drogon::Post,"/api/steam/imports",empty,true),
        drogon::k403Forbidden,"cross_site_request_forbidden");
    REQUIRE(repository.list().empty());
    auto first = send(drogon::Post,"/api/steam/imports",empty);
    REQUIRE(first->statusCode() == drogon::k200OK);
    REQUIRE(json(first)["imported"].asInt() == 1);
    REQUIRE(json(first)["updated"].asInt() == 0);
    REQUIRE(json(first)["warnings"].isArray());
    REQUIRE_FALSE(json(first)["warnings"].empty());
    assertNoSecret(first);
    const auto games = repository.list();
    REQUIRE(games.size() == 1);
    REQUIRE(games.front().steamAppId == "570");
    REQUIRE(games.front().path == "steam://rungameid/570");
    REQUIRE(games.front().title == "Offline Game");
    REQUIRE_FALSE(games.front().playtimeMinutes);
    REQUIRE_FALSE(games.front().achievementsUnlocked);
    REQUIRE_FALSE(games.front().achievementsTotal);
    REQUIRE(games.front().steamStatsError == "steam_credentials_required");
    auto second = send(drogon::Post,"/api/steam/imports",empty);
    REQUIRE(second->statusCode() == drogon::k200OK);
    REQUIRE(json(second)["imported"].asInt() == 0);
    REQUIRE(json(second)["updated"].asInt() == 1);
    REQUIRE(repository.list().size() == 1);
    REQUIRE(repository.list().front().id == games.front().id);
    error(send(drogon::Post,"/api/steam/login",empty,true),drogon::k403Forbidden,"cross_site_request_forbidden");
    auto started=send(drogon::Post,"/api/steam/login",empty);
    REQUIRE(started->statusCode()==drogon::k200OK);
    REQUIRE_FALSE(json(started).isMember("token"));
    const auto statusPath="/api/steam/login?sessionId="+json(started)["sessionId"].asString();
    REQUIRE(json(send(drogon::Get,statusPath))["state"].asString()=="pending");
    Json::Value complete; complete["token"]=json(started)["sessionId"]; complete["steamId"]="76561198000000000"; complete["apiKey"]=secret;
    error(send(drogon::Post,"/api/steam/login/complete",complete),drogon::k403Forbidden,"steam_login_invalid_token");
    complete["token"]=launcher.args[4];
    error(send(drogon::Post,"/api/steam/login/complete",complete,true),drogon::k403Forbidden,"cross_site_request_forbidden");
    auto completed=send(drogon::Post,"/api/steam/login/complete",complete);
    REQUIRE(completed->statusCode()==drogon::k200OK); assertNoSecret(completed);
    REQUIRE(config->current().steamId=="76561198000000000");
    REQUIRE(json(send(drogon::Get,statusPath))["state"].asString()=="completed");
    error(send(drogon::Post,"/api/steam/login/complete",complete),drogon::k403Forbidden,"steam_login_invalid_token");
    REQUIRE(networkCalls == 0);
#endif
}
