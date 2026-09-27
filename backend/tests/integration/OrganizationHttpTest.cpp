#include "anime_vault/api/MediaController.hpp"
#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <drogon/drogon.h>

#include <catch2/catch_test_macros.hpp>
#include <json/json.h>
#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace anime_vault::api;
namespace fs = std::filesystem;

namespace {
struct OfflineTransport final : anime_vault::BangumiTransport {
    std::optional<Response> subjectResponse;
    void search(std::string, Completion completion) override { completion(std::nullopt, "offline"); }
    void subject(std::int64_t, Completion completion) override {
        completion(subjectResponse, subjectResponse ? "" : "offline");
    }
};
std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

TEST_CASE("play request accepts only a positive media ID and a local browser origin") {
    REQUIRE(parsePlayMediaRequest("7").mediaId == 7);
    for (const auto& id : {"0", "-1", "abc", "7/../8"}) {
        try { (void)parsePlayMediaRequest(id); FAIL("expected invalid ID"); }
        catch (const ApiError& error) { REQUIRE(error.code == "invalid_id"); }
    }
    REQUIRE(playbackOriginAllowed("http://127.0.0.1:5173"));
    REQUIRE(playbackOriginAllowed("http://localhost:5173"));
    REQUIRE_FALSE(playbackOriginAllowed("https://evil.example"));
    REQUIRE_FALSE(playbackOriginAllowed("http://127.0.0.1.evil.example:5173"));
}
}

TEST_CASE("organization execution HTTP DTO requires explicit confirmation and rejects paths") {
    Json::Value body;
    body["planId"] = Json::Int64(7);
    body["idempotencyKey"] = "execution-7";
    auto rejected = [&](const std::string& expected) {
        try { (void)parseExecuteOrganizationRequest(body); FAIL("expected rejection"); }
        catch (const ApiError& error) {
            REQUIRE(error.status == 400);
            REQUIRE(error.code == expected);
        }
    };
    rejected("confirmation_required");
    body["confirmed"] = true;
    body["sourcePath"] = "untrusted";
    rejected("invalid_request");
    body.removeMember("sourcePath");
    const auto request = parseExecuteOrganizationRequest(body);
    REQUIRE(request.planId == 7);
    REQUIRE(request.idempotencyKey == "execution-7");
    REQUIRE(request.confirmed);
    REQUIRE_FALSE(request.qbDownloadComplete);
    body["planId"] = 0;
    rejected("invalid_plan_id");
    body["planId"] = Json::Int64(7);
    body["idempotencyKey"] = std::string(129, 'a');
    rejected("invalid_idempotency_key");
    body["idempotencyKey"] = "execution-7";
    body["qbDownloadComplete"] = "true";
    rejected("invalid_request");
    body["qbDownloadComplete"] = true;
    REQUIRE(parseExecuteOrganizationRequest(body).qbDownloadComplete);
}

TEST_CASE("Bangumi bind DTO accepts only confirmed positive subject IDs") {
    Json::Value body;
    body["subjectId"] = Json::Int64(123);
    auto rejected = [&](const std::string& code) {
        try { (void)parseBindAnimeRequest(body); FAIL("expected rejection"); }
        catch (const ApiError& error) { REQUIRE(error.code == code); }
    };
    rejected("confirmation_required");
    body["confirmed"] = true;
    body["name"] = "untrusted";
    rejected("invalid_request");
    body.removeMember("name");
    REQUIRE(parseBindAnimeRequest(body).subjectId == 123);
    body["subjectId"] = 0;
    rejected("bangumi_invalid_subject_id");
}

TEST_CASE("organization execution route confirms a disposable import plan") {
    const auto root = fs::canonical(fs::temp_directory_path()) /
        ("anime-vault-http-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto qb = root / "qb";
    const auto imported = root / "import";
    const auto library = root / "library";
    fs::create_directories(qb);
    fs::create_directories(imported);
    fs::create_directories(library);
    const auto source = imported / "Episode.mkv";
    { std::ofstream file(source, std::ios::binary); file << "disposable episode"; }
    const auto target = library / "Show" / "01.mkv";
    anime_vault::SqliteDatabase database(root / "test.db");
    database.migrate();
    anime_vault::SqliteMediaRepository repository(database);
    const auto scan = repository.createScan({0, "external_import", "completed"});
    anime_vault::MediaRecord media{0, scan, utf8(source), utf8(source.filename()), "1", "normal",
        static_cast<std::int64_t>(fs::file_size(source)), "inbox", 1.0};
    media.origin = "external_import";
    media.sourceModifiedAt = std::to_string(fs::last_write_time(source).time_since_epoch().count());
    const auto mediaId = repository.insertMedia(media);
    REQUIRE(sqlite3_exec(database.handle(),
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<100) "
        "INSERT INTO media_file(scan_id,source_path,filename,size_bytes,status) "
        "SELECT 1,printf('C:/disposable/page-%d.mkv',x),printf('page-%d.mkv',x),1,'inbox' FROM n;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto planId = repository.insertPlan({0, mediaId, utf8(source), media.sizeBytes,
        media.sourceModifiedAt, utf8(target), "hardlink", "2099-01-01T00:00:00Z", "pending", "preview-http"});
    MediaService mediaService(repository, qb, library, imported);
    anime_vault::OrganizationService organization(repository, qb, imported, library);
    registerMediaEndpoints(mediaService, organization);
    OfflineTransport offlineTransport;
    auto bangumi = std::make_shared<anime_vault::BangumiService>(repository, offlineTransport);
    registerAnimeEndpoints(repository, bangumi);
    drogon::app().addListener("127.0.0.1", 18849);
    std::thread server([] { drogon::app().run(); });
    struct StopServer { std::thread& thread; ~StopServer() { drogon::app().quit(); thread.join(); } } stop{server};
    for (int attempt = 0; attempt < 100 && !drogon::app().isRunning(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18849");
    auto inboxGet = [&](const std::string& path) {
        auto request = drogon::HttpRequest::newHttpRequest();
        request->setMethod(drogon::Get); request->setPath(path);
        const auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response;
    };
    const auto firstPage = inboxGet("/api/inbox?limit=100&offset=0");
    REQUIRE(firstPage->statusCode() == drogon::k200OK);
    REQUIRE((*firstPage->getJsonObject())["items"].size() == 100);
    REQUIRE((*firstPage->getJsonObject())["total"].asInt64() == 101);
    REQUIRE((*firstPage->getJsonObject())["nextOffset"].asInt64() == 100);
    const auto lastPage = inboxGet("/api/inbox?limit=100&offset=100");
    REQUIRE((*lastPage->getJsonObject())["items"].size() == 1);
    REQUIRE((*lastPage->getJsonObject())["items"][0]["filename"].asString() == "page-100.mkv");
    REQUIRE((*lastPage->getJsonObject())["nextOffset"].isNull());
    const auto importedPage = inboxGet("/api/inbox?origin=external_import");
    REQUIRE(importedPage->statusCode() == drogon::k200OK);
    REQUIRE((*importedPage->getJsonObject())["total"].asInt64() == 1);
    REQUIRE((*importedPage->getJsonObject())["items"][0]["origin"].asString() == "external_import");
    const auto invalidOrigin = inboxGet("/api/inbox?origin=invalid");
    REQUIRE(invalidOrigin->statusCode() == drogon::k400BadRequest);
    REQUIRE((*invalidOrigin->getJsonObject())["error"]["code"].asString() == "invalid_origin");
    REQUIRE((*inboxGet("/api/inbox?limit=101")->getJsonObject())["error"]["code"].asString() == "invalid_page");
    REQUIRE((*inboxGet("/api/inbox?offset=-1")->getJsonObject())["error"]["code"].asString() == "invalid_page");
    Json::Value correction;
    correction["title"] = "Show";
    correction["season"] = "1";
    correction["episodeNumber"] = "1";
    correction["episodeType"] = "normal";
    correction["bangumiSubjectId"] = Json::Int64(123);
    auto bypass = drogon::HttpRequest::newHttpJsonRequest(correction);
    bypass->setMethod(drogon::Post);
    bypass->setPath("/api/inbox/" + std::to_string(mediaId) + "/parse");
    const auto [bypassResult, bypassResponse] = client->sendRequest(bypass);
    REQUIRE(bypassResult == drogon::ReqResult::Ok);
    REQUIRE(bypassResponse->statusCode() == drogon::k400BadRequest);
    REQUIRE((*bypassResponse->getJsonObject())["error"]["code"].asString() == "binding_requires_confirmation");
    correction.removeMember("bangumiSubjectId");
    auto localCorrection = drogon::HttpRequest::newHttpJsonRequest(correction);
    localCorrection->setMethod(drogon::Post);
    localCorrection->setPath("/api/inbox/" + std::to_string(mediaId) + "/parse");
    const auto [correctionResult, correctionResponse] = client->sendRequest(localCorrection);
    REQUIRE(correctionResult == drogon::ReqResult::Ok);
    REQUIRE(correctionResponse->statusCode() == drogon::k200OK);
    REQUIRE(repository.listAnime().size() == 1);
    auto post = [&](const Json::Value& body) {
        auto request = drogon::HttpRequest::newHttpJsonRequest(body);
        request->setMethod(drogon::Post);
        request->setPath("/api/organize/execute");
        request->addHeader("X-Request-Id", "organization-http-test");
        const auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response;
    };
    Json::Value body;
    body["planId"] = Json::Int64(planId);
    body["idempotencyKey"] = "execute-http";
    const auto missingConfirmation = post(body);
    REQUIRE(missingConfirmation->statusCode() == drogon::k400BadRequest);
    REQUIRE((*missingConfirmation->getJsonObject())["error"]["code"].asString() == "confirmation_required");
    REQUIRE((*missingConfirmation->getJsonObject())["requestId"].asString() == "organization-http-test");
    REQUIRE_FALSE(fs::exists(target));
    auto oversized = drogon::HttpRequest::newHttpRequest();
    oversized->setMethod(drogon::Post);
    oversized->setPath("/api/organize/execute");
    oversized->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    oversized->setBody(std::string(4097, 'x'));
    const auto [oversizedResult, oversizedResponse] = client->sendRequest(oversized);
    REQUIRE(oversizedResult == drogon::ReqResult::Ok);
    REQUIRE(oversizedResponse->statusCode() == drogon::k400BadRequest);
    REQUIRE((*oversizedResponse->getJsonObject())["error"]["code"].asString() == "request_too_large");
    auto withQuery = drogon::HttpRequest::newHttpJsonRequest(body);
    withQuery->setMethod(drogon::Post);
    withQuery->setPath("/api/organize/execute?sourcePath=untrusted");
    const auto [queryResult, queryResponse] = client->sendRequest(withQuery);
    REQUIRE(queryResult == drogon::ReqResult::Ok);
    REQUIRE(queryResponse->statusCode() == drogon::k400BadRequest);
    REQUIRE((*queryResponse->getJsonObject())["error"]["code"].asString() == "invalid_request");
    auto malformed = drogon::HttpRequest::newHttpRequest();
    malformed->setMethod(drogon::Post);
    malformed->setPath("/api/organize/execute");
    malformed->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    malformed->setBody("{");
    const auto [malformedResult, malformedResponse] = client->sendRequest(malformed);
    REQUIRE(malformedResult == drogon::ReqResult::Ok);
    REQUIRE(malformedResponse->statusCode() == drogon::k400BadRequest);
    REQUIRE((*malformedResponse->getJsonObject())["error"]["code"].asString() == "invalid_request");
    body["confirmed"] = true;
    const auto executed = post(body);
    REQUIRE(executed->statusCode() == drogon::k200OK);
    const auto payload = executed->getJsonObject();
    REQUIRE((*payload)["jobId"].asInt64() > 0);
    REQUIRE((*payload)["targetPath"].asString() == utf8(target));
    REQUIRE((*payload)["bytes"].asInt64() == media.sizeBytes);
    REQUIRE((*payload)["status"].asString() == "completed");
    REQUIRE((*payload)["requestId"].asString() == "organization-http-test");
    REQUIRE(fs::exists(source));
    REQUIRE(fs::exists(target));
    const auto qbSource = qb / "Download.mkv";
    { std::ofstream file(qbSource, std::ios::binary); file << "complete download"; }
    const auto qbScan = repository.createScan({0, "qb_download", "completed"});
    auto qbMedia = media;
    qbMedia.id = 0;
    qbMedia.scanId = qbScan;
    qbMedia.sourcePath = utf8(qbSource);
    qbMedia.filename = utf8(qbSource.filename());
    qbMedia.sizeBytes = static_cast<std::int64_t>(fs::file_size(qbSource));
    qbMedia.sourceModifiedAt = std::to_string(fs::last_write_time(qbSource).time_since_epoch().count());
    qbMedia.origin = "qb_download";
    const auto qbMediaId = repository.insertMedia(qbMedia);
    const auto qbTarget = library / "Show" / "02.mkv";
    const auto qbPlanId = repository.insertPlan({0, qbMediaId, utf8(qbSource), qbMedia.sizeBytes,
        qbMedia.sourceModifiedAt, utf8(qbTarget), "hardlink", "2099-01-01T00:00:00Z", "pending", "preview-qb-http"});
    body["planId"] = Json::Int64(qbPlanId);
    body["idempotencyKey"] = "execute-qb-http";
    const auto incomplete = post(body);
    REQUIRE(incomplete->statusCode() == drogon::k400BadRequest);
    REQUIRE((*incomplete->getJsonObject())["error"]["code"].asString() == "qb_completion_required");
    REQUIRE_FALSE(fs::exists(qbTarget));
    body["qbDownloadComplete"] = true;
    const auto qbExecuted = post(body);
    REQUIRE(qbExecuted->statusCode() == drogon::k200OK);
    REQUIRE((*qbExecuted->getJsonObject())["targetPath"].asString() == utf8(qbTarget));
    REQUIRE(fs::exists(qbSource));
    bool limited = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        body["idempotencyKey"] = "throttle-" + std::to_string(attempt);
        const auto response = post(body);
        if (response->statusCode() == drogon::k429TooManyRequests) {
            REQUIRE((*response->getJsonObject())["error"]["code"].asString() == "execute_rate_limited");
            limited = true;
            break;
        }
    }
    REQUIRE(limited);
    body["idempotencyKey"] = "execute-qb-http";
    const auto replay = post(body);
    REQUIRE(replay->statusCode() == drogon::k200OK);
    REQUIRE((*replay->getJsonObject())["jobId"].asInt64() ==
        (*qbExecuted->getJsonObject())["jobId"].asInt64());
    REQUIRE(sqlite3_exec(database.handle(),
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<201) "
        "INSERT INTO anime(display_title) SELECT printf('Extra %d',x) FROM n;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    auto listRequest = drogon::HttpRequest::newHttpRequest();
    listRequest->setMethod(drogon::Get);
    listRequest->setPath("/api/anime?limit=100&offset=200");
    const auto [listResult, listResponse] = client->sendRequest(listRequest);
    REQUIRE(listResult == drogon::ReqResult::Ok);
    REQUIRE(listResponse->statusCode() == drogon::k200OK);
    REQUIRE((*listResponse->getJsonObject())["items"].size() == 2);
    REQUIRE((*listResponse->getJsonObject())["nextOffset"].isNull());
    listRequest->setPath("/api/anime?limit=100&offset=0");
    const auto [firstResult, firstResponse] = client->sendRequest(listRequest);
    REQUIRE(firstResult == drogon::ReqResult::Ok);
    REQUIRE((*firstResponse->getJsonObject())["nextOffset"].asInt64() == 100);
    REQUIRE(sqlite3_exec(database.handle(),
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<501) "
        "INSERT INTO media_file(anime_id,source_path,filename,size_bytes,status) "
        "SELECT 1,printf('C:/test/page-%d.mkv',x),printf('page-%d.mkv',x),1,'inbox' FROM n;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    auto detailRequest = drogon::HttpRequest::newHttpRequest();
    detailRequest->setMethod(drogon::Get);
    detailRequest->setPath("/api/anime/1?mediaLimit=100&mediaOffset=500");
    const auto [detailResult, detailResponse] = client->sendRequest(detailRequest);
    REQUIRE(detailResult == drogon::ReqResult::Ok);
    REQUIRE(detailResponse->statusCode() == drogon::k200OK);
    REQUIRE((*detailResponse->getJsonObject())["media"].size() == 2);
    REQUIRE((*detailResponse->getJsonObject())["nextMediaOffset"].isNull());
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title,bangumi_subject_id) VALUES('Owner',123);",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    offlineTransport.subjectResponse = anime_vault::BangumiTransport::Response{
        200, R"({"id":123,"type":2,"name":"Frieren"})"};
    Json::Value bindBody;
    bindBody["subjectId"] = Json::Int64(123);
    bindBody["confirmed"] = true;
    auto bindRequest = drogon::HttpRequest::newHttpJsonRequest(bindBody);
    bindRequest->setMethod(drogon::Put);
    bindRequest->setPath("/api/anime/1/bangumi");
    const auto [bindResult, bindResponse] = client->sendRequest(bindRequest);
    REQUIRE(bindResult == drogon::ReqResult::Ok);
    REQUIRE(bindResponse->statusCode() == drogon::k409Conflict);
    REQUIRE((*bindResponse->getJsonObject())["error"]["code"].asString() == "bangumi_subject_in_use");
}
