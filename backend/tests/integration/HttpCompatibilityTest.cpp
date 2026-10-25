#include "anime_vault/infrastructure/NativeLibraryFileAccess.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteNovelRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include "anime_vault/api/BangumiConfigController.hpp"
#include "anime_vault/api/GameController.hpp"
#include "anime_vault/api/MediaController.hpp"
#include "anime_vault/api/NovelController.hpp"
#include "anime_vault/api/QbConfigController.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>
#include <sqlite3.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

namespace {
namespace fs = std::filesystem;
using namespace anime_vault;

struct DisposableDirectory {
    fs::path path = fs::temp_directory_path() /
        ("anime-vault-http-compatibility-" + std::to_string(std::random_device{}()));
    DisposableDirectory() { fs::create_directories(path); }
    ~DisposableDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};

struct OfflineTransport final : BangumiTransport {
    std::atomic_int calls{};
    void search(std::string, Completion done) override { ++calls; done({}, "offline"); }
    void searchAliases(std::string, Completion done) override { ++calls; done({}, "offline"); }
    void searchBooks(std::string, Completion done) override { ++calls; done({}, "offline"); }
    void searchGames(std::string, Completion done) override { ++calls; done({}, "offline"); }
    void subject(std::int64_t, Completion done) override { ++calls; done({}, "offline"); }
};

struct OfflineImages final : CoverImageFetcher {
    std::atomic_int calls{};
    void fetch(std::string, Completion done) override { ++calls; done({}, "offline"); }
};

struct NoProcessLauncher final : ProcessLauncher {
    std::atomic_int calls{};
    bool launch(const fs::path&, const std::vector<std::string>&) override { ++calls; return false; }
    bool openDefault(const fs::path&) override { ++calls; return false; }
};

Json::Value json(const drogon::HttpResponsePtr& response) {
    REQUIRE(response->getJsonObject());
    REQUIRE(response->getJsonObject()->isObject());
    return *response->getJsonObject();
}

void requestId(const drogon::HttpResponsePtr& response, const std::string& expected) {
    REQUIRE(response->getHeader("X-Request-Id") == expected);
    REQUIRE(json(response)["requestId"].asString() == expected);
}

void error(const drogon::HttpResponsePtr& response, drogon::HttpStatusCode status,
           const std::string& code) {
    REQUIRE(response->statusCode() == status);
    const auto value = json(response);
    REQUIRE(value["error"]["code"].asString() == code);
    REQUIRE(value["error"]["message"].isString());
    REQUIRE_FALSE(value["error"]["message"].asString().empty());
    REQUIRE_FALSE(value.isMember("errorCode"));
    REQUIRE(value["requestId"].asString() == response->getHeader("X-Request-Id"));
}
}

TEST_CASE("HTTP families preserve their existing compatibility differences") {
    DisposableDirectory directory;
    SqliteDatabase database(directory.path / "test.db");
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(), "INSERT INTO anime(display_title) VALUES('Local');",
                        nullptr, nullptr, nullptr) == SQLITE_OK);
    std::ofstream(directory.path / "Story.txt") << "disposable book";
    std::ofstream(directory.path / "Story.exe") << "MZ disposable executable fixture";
    SqliteNovelRepository novelsRepository(database);
    NativeLibraryFileAccess novelsFiles;
    auto novels = std::make_shared<NovelService>(novelsRepository, novelsFiles);
    novels->importPath(directory.path / "Story.txt");
    const auto novelId = novels->list().front().id;
    SqliteGameRepository gamesRepository(database);
    auto games = std::make_shared<GameService>(gamesRepository, novelsFiles);
    const auto gameId = games->importExecutable(directory.path / "Story.exe").id;
    OfflineTransport transport;
    OfflineImages images;
    NoProcessLauncher launcher;
    auto bangumi = std::make_shared<BangumiService>(repository, transport);
    auto novelScraper = std::make_shared<NovelBangumiService>(*novels, transport, images);
    auto gameScraper = std::make_shared<GameBangumiService>(*games, transport, images);
    auto qbConfig = std::make_shared<QbConnectionManager>(directory.path / "qb",
        QbConnectionConfig{"http://127.0.0.1:18859", "", ""});
    auto bangumiConfig = std::make_shared<BangumiConnectionManager>(database, "");
    api::registerManagementEndpoints(repository, {directory.path / "source", directory.path / "import",
        directory.path / "library", directory.path / "data"});
    api::registerAnimeEndpoints(repository, bangumi);
    api::registerNovelEndpoints(novels, novelScraper, launcher);
    api::registerGameEndpoints(games, gameScraper, launcher);
    api::registerQbConfigEndpoints(qbConfig);
    api::registerBangumiConfigEndpoints(bangumiConfig);

    // 每个进程只启动一次 Drogon，所有接口族共用此回环服务。
    drogon::app().addListener("127.0.0.1", 18859);
    std::thread server([] { drogon::app().run(); });
    struct Stop {
        std::thread& thread;
        ~Stop() { drogon::app().quit(); thread.join(); }
    } stop{server};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18859");
    const auto send = [&](drogon::HttpMethod method, const std::string& path,
                          const Json::Value& body = Json::Value(),
                          const std::string& suppliedId = "client-compatibility") {
        INFO(method << " " << path);
        auto request = body.isNull() ? drogon::HttpRequest::newHttpRequest()
                                     : drogon::HttpRequest::newHttpJsonRequest(body);
        request->setMethod(method);
        request->setPath(path);
        if (!suppliedId.empty()) request->addHeader("X-Request-Id", suppliedId);
        auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response;
    };

    const auto anime = send(drogon::Get, "/api/anime/1");
    REQUIRE(anime->statusCode() == drogon::k200OK);
    requestId(anime, "client-compatibility");
    const auto animeJson = json(anime);
    REQUIRE_FALSE(animeJson.isMember("year"));
    REQUIRE_FALSE(animeJson.isMember("bangumiSubjectId"));
    REQUIRE(animeJson.isMember("nextMediaOffset"));
    REQUIRE(animeJson["nextMediaOffset"].isNull());
    const auto scans = send(drogon::Get, "/api/scans", Json::Value(), "");
    requestId(scans, "1");
    REQUIRE(json(scans).isMember("nextOffset"));
    REQUIRE(json(scans)["nextOffset"].isNull());

    const auto novelPath = "/api/novels/" + std::to_string(novelId);
    const auto gamePath = "/api/games/" + std::to_string(gameId);
    const auto novel = send(drogon::Get, novelPath);
    REQUIRE(novel->statusCode() == drogon::k200OK);
    requestId(novel, "novel-1");
    const auto novelJson = json(novel);
    REQUIRE(novelJson.isMember("subjectId"));
    REQUIRE(novelJson["subjectId"].isNull());
    REQUIRE(novelJson["files"][0].isMember("subjectId"));
    REQUIRE(novelJson["files"][0]["subjectId"].isNull());
    const auto game = send(drogon::Get, "/api/games");
    REQUIRE(game->statusCode() == drogon::k200OK);
    requestId(game, "game-1");
    REQUIRE(json(game)["items"][0].isMember("subjectId"));
    REQUIRE(json(game)["items"][0]["subjectId"].isNull());
    const auto qb = send(drogon::Get, "/api/qb/config");
    requestId(qb, "1");
    REQUIRE_FALSE(json(qb).isMember("password"));
    REQUIRE_FALSE(json(qb)["configured"].asBool());
    const auto config = send(drogon::Get, "/api/bangumi/config");
    requestId(config, "1");
    REQUIRE_FALSE(json(config)["configured"].asBool());

    Json::Value extra(Json::objectValue);
    extra["extra"] = true;
    auto animeEdit = extra;
    animeEdit["displayTitle"] = "Local";
    animeEdit["season"] = "";
    animeEdit["coverUrl"] = "";
    const auto invalidMedia = send(drogon::Put, "/api/anime/1", animeEdit);
    error(invalidMedia, drogon::k400BadRequest, "invalid_anime_edit");
    requestId(invalidMedia, "client-compatibility");
    const auto invalidNovel = send(drogon::Post, novelPath + "/scrape", extra);
    error(invalidNovel, drogon::k400BadRequest, "invalid_request");
    requestId(invalidNovel, "novel-2");
    const auto invalidGame = send(drogon::Post, gamePath + "/scrape", extra);
    error(invalidGame, drogon::k400BadRequest, "invalid_request");
    requestId(invalidGame, "game-2");
    auto qbEdit = extra;
    qbEdit["url"] = "http://127.0.0.1:18859";
    qbEdit["username"] = "";
    qbEdit["password"] = "";
    error(send(drogon::Put, "/api/qb/config", qbEdit), drogon::k400BadRequest, "invalid_request");
    auto bangumiEdit = extra;
    bangumiEdit["userAgent"] = "test/1.0 (https://example.com)";
    error(send(drogon::Put, "/api/bangumi/config", bangumiEdit), drogon::k400BadRequest, "invalid_request");

    for (const auto* id : {"0", "-1", "bad", "9223372036854775808"})
        error(send(drogon::Get, std::string("/api/anime/") + id), drogon::k400BadRequest, "invalid_id");
    error(send(drogon::Get, "/api/novels/-1"), drogon::k404NotFound, "novel_not_found");
    Json::Value confirmed;
    confirmed["confirmed"] = true;
    error(send(drogon::Post, "/api/games/-1/launch", confirmed), drogon::k404NotFound, "game_not_found");

    // 刮削失败仍是 HTTP 200，错误位于业务字段而非通用 error 对象。
    for (const auto& path : {novelPath, gamePath}) {
        const auto scrape = send(drogon::Post, path + "/scrape", Json::Value(Json::objectValue));
        REQUIRE(scrape->statusCode() == drogon::k200OK);
        const auto value = json(scrape);
        REQUIRE(value["errorCode"].asString() == "offline");
        REQUIRE_FALSE(value["bound"].asBool());
        REQUIRE_FALSE(value["coverUpdated"].asBool());
        REQUIRE_FALSE(value.isMember("error"));
        REQUIRE(value.isMember("source") == (path == gamePath));
        if (path == gamePath) REQUIRE(value["source"].asString() == "bangumi");
    }
    const auto animeCover = send(drogon::Get, "/api/covers/1/555/missing.jpg");
    REQUIRE(animeCover->statusCode() == drogon::k404NotFound);
    REQUIRE(animeCover->getBody().empty());
    REQUIRE(animeCover->getHeader("X-Request-Id").empty());
    REQUIRE_FALSE(animeCover->getJsonObject());
    error(send(drogon::Get, novelPath + "/cover"), drogon::k404NotFound, "cover_unavailable");
    error(send(drogon::Get, gamePath + "/cover"), drogon::k400BadRequest, "cover_unavailable");
    REQUIRE(transport.calls == 2);
    REQUIRE(images.calls == 0);
    REQUIRE(launcher.calls == 0);
}
