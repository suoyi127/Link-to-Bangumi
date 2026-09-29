#include "anime_vault/api/MediaController.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/services/BangumiService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>
#include <sqlite3.h>
#include <filesystem>
#include <random>
#include <thread>

namespace fs = std::filesystem;

namespace {
struct OfflineTransport final : anime_vault::BangumiTransport {
    void search(std::string, Completion done) override { done(std::nullopt, "offline"); }
    void searchAliases(std::string, Completion done) override { done(std::nullopt, "offline"); }
    void subject(std::int64_t, Completion done) override { done(std::nullopt, "offline"); }
};
}

TEST_CASE("management routes paginate and persist only allowed preferences") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-management-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root);
    anime_vault::SqliteDatabase database(root / "test.db");
    database.migrate();
    anime_vault::SqliteMediaRepository repository(database);
    for (int i = 0; i < 3; ++i) {
        anime_vault::ScanRecord scan{0, "external_import", "completed", i};
        if (i == 2) {
            scan.status = "failed";
            scan.errorSummary = "C:/private/media/secret.mkv: access denied";
        }
        repository.createScan(scan);
    }
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Local');"
        "INSERT INTO media_file(anime_id,source_path,filename,size_bytes,status) VALUES(1,'C:/disposable/a.mkv','a.mkv',1,'inbox');"
        "INSERT INTO audit_log(action,entity_type,entity_id,details_json) VALUES"
        "('anime_bound','anime','1','{\"secret\":\"hidden\"}'),"
        "('other','scan_job','2','{\"secret\":\"hidden\"}'),"
        "('other','scan_job','3','{\"secret\":\"hidden\"}');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(repository.getMedia(1));
    REQUIRE(repository.getMedia(1)->animeId == 1);
    REQUIRE(repository.getAnime(1));
    REQUIRE(repository.getAnime(1)->media.front().animeId == 1);
    anime_vault::api::registerManagementEndpoints(repository,
        {root / "source", root / "import", root / "library", root / "data", true});
    OfflineTransport transport;
    auto bangumi = std::make_shared<anime_vault::BangumiService>(repository, transport);
    anime_vault::api::registerAnimeEndpoints(repository, bangumi);
    drogon::app().addListener("127.0.0.1", 18850);
    std::thread server([] { drogon::app().run(); });
    struct Stop { std::thread& thread; ~Stop() { drogon::app().quit(); thread.join(); } } stop{server};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18850");
    auto get = [&](const std::string& path) {
        auto request = drogon::HttpRequest::newHttpRequest();
        request->setMethod(drogon::Get); request->setPath(path);
        auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response;
    };
    auto scans = get("/api/scans?limit=2&offset=0");
    auto detail = get("/api/anime/1");
    REQUIRE(detail->statusCode() == drogon::k200OK);
    REQUIRE((*detail->getJsonObject())["media"][0]["animeId"].asInt64() == 1);
    REQUIRE(scans->statusCode() == drogon::k200OK);
    REQUIRE((*scans->getJsonObject())["items"].size() == 2);
    REQUIRE((*scans->getJsonObject())["items"][0]["id"].asInt64() == 3);
    REQUIRE((*scans->getJsonObject())["items"][0]["errorSummary"].asString() == "scan_failed");
    REQUIRE((*scans->getJsonObject())["nextOffset"].asInt64() == 2);
    auto audit = get("/api/audit-logs?limit=2&offset=0");
    REQUIRE(audit->statusCode() == drogon::k200OK);
    REQUIRE((*audit->getJsonObject())["items"].size() == 2);
    REQUIRE_FALSE((*audit->getJsonObject())["items"][0].isMember("detailsJson"));
    REQUIRE((*audit->getJsonObject())["nextOffset"].asInt64() == 2);
    auto filtered = get("/api/audit-logs?animeId=1");
    REQUIRE((*filtered->getJsonObject())["items"].size() == 1);
    auto settings = get("/api/settings");
    REQUIRE((*settings->getJsonObject())["bangumiConfigured"].asBool());
    REQUIRE_FALSE((*settings->getJsonObject())["qbWebUiConfigured"].asBool());
    REQUIRE_FALSE((*settings->getJsonObject())["qbDownloadConfigured"].asBool());
    const auto selected = root / "selected-qb";
    fs::create_directories(selected);
    fs::create_directories(root / "import");
    auto selectDirectory = [&](const std::string& path) {
        Json::Value selection;
        selection["path"] = path;
        auto request = drogon::HttpRequest::newHttpJsonRequest(selection);
        request->setMethod(drogon::Put);
        request->setPath("/api/settings/qb-download-directory");
        auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        return response;
    };
    const auto selectedBytes = selected.u8string();
    const std::string selectedUtf8(reinterpret_cast<const char*>(selectedBytes.data()), selectedBytes.size());
    const auto canonicalBytes = fs::canonical(selected).u8string();
    const std::string canonicalUtf8(reinterpret_cast<const char*>(canonicalBytes.data()), canonicalBytes.size());
    auto selectedResponse = selectDirectory(selectedUtf8);
    REQUIRE(selectedResponse->statusCode() == drogon::k200OK);
    REQUIRE(selectedResponse->getJsonObject());
    REQUIRE((*selectedResponse->getJsonObject())["restartRequired"].asBool());
    REQUIRE((*get("/api/settings")->getJsonObject())["qbDownloadDirectory"].asString() == canonicalUtf8);
    REQUIRE((*selectDirectory("relative-qb")->getJsonObject())["error"]["code"].asString() ==
        "invalid_qb_download_directory");
    const auto importBytes = (root / "import").u8string();
    REQUIRE((*selectDirectory(std::string(reinterpret_cast<const char*>(importBytes.data()), importBytes.size()))
        ->getJsonObject())["error"]["code"].asString() == "overlapping_roots");
    const auto sourcePath = (*settings->getJsonObject())["sourcePath"].asString();
    Json::Value body;
    body["preferredOperation"] = "copy";
    body["scanIntervalSeconds"] = 60;
    body["mpvExecutable"] = "mpv";
    body["qbWebUiUrl"] = "http://127.0.0.1:8080";
    auto put = [&](const Json::Value& value) {
        auto request = drogon::HttpRequest::newHttpJsonRequest(value);
        request->setMethod(drogon::Put); request->setPath("/api/settings");
        auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        return response;
    };
    REQUIRE(put(body)->statusCode() == drogon::k200OK);
    settings = get("/api/settings");
    REQUIRE((*settings->getJsonObject())["preferredOperation"].asString() == "copy");
    REQUIRE((*settings->getJsonObject())["sourcePath"].asString() == sourcePath);
    REQUIRE((*settings->getJsonObject())["qbDownloadDirectory"].asString() == canonicalUtf8);
    body["sourcePath"] = "C:/untrusted";
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "invalid_request");
    body.removeMember("sourcePath");
    body["qbWebUiUrl"] = "http://user:secret@127.0.0.1:8080";
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "invalid_settings");
    body["qbWebUiUrl"] = "http://127.0.0.1:8080/?token=hidden";
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "invalid_settings");
    body["qbWebUiUrl"] = "http://localhost:8080/\r\nX-Test: 1";
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "invalid_settings");
    body["qbWebUiUrl"] = "http://127.0.0.1:8080";
    body["mpvExecutable"] = std::string("mpv\0hidden", 10);
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "invalid_settings");
    body["mpvExecutable"] = "mpv";
    body["qbWebUiUrl"] = "http://localhost:0";
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "invalid_settings");
    body["qbWebUiUrl"] = "http://127.0.0.1:99999";
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "invalid_settings");
    body["qbWebUiUrl"] = "http://127.0.0.1:8080";
    body["mpvExecutable"] = std::string(5000, 'x');
    REQUIRE((*put(body)->getJsonObject())["error"]["code"].asString() == "request_too_large");
    REQUIRE((*get("/api/scans?limit=101")->getJsonObject())["error"]["code"].asString() == "invalid_page");
    REQUIRE((*get("/api/audit-logs?offset=-1")->getJsonObject())["error"]["code"].asString() == "invalid_page");
}
