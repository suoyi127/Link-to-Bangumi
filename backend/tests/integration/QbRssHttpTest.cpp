#include "anime_vault/api/QbRssController.hpp"
#include "anime_vault/api/MediaService.hpp"
#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>
#include <random>
#include <atomic>
#include <mutex>
#include <thread>

TEST_CASE("qB RSS HTTP boundary preserves offline responses validation and directory errors") {
    const auto data = std::filesystem::temp_directory_path() /
        ("anime-vault-qb-rss-http-" + std::to_string(std::random_device{}()));
    auto manager = std::make_shared<anime_vault::QbConnectionManager>(data,
        anime_vault::QbConnectionConfig{"http://[::1]:8080", "", ""});
    std::mutex directoryMutex;
    std::string directoryError;
    std::atomic_bool sourceReady{true};
    std::atomic_bool neutralFailure{false};
    std::atomic_int validations{0};
    const auto setDirectoryError = [&](std::string value) {
        std::lock_guard lock(directoryMutex);
        directoryError = std::move(value);
    };
    anime_vault::api::registerQbRssEndpoints(manager, data / "source",
        [&] { std::lock_guard lock(directoryMutex); return directoryError; }, [&] {
            ++validations;
            if (!sourceReady && neutralFailure)
                throw anime_vault::MediaError(anime_vault::MediaFailureKind::conflict, "qb_source_unavailable", "source directory unavailable");
            if (!sourceReady) throw anime_vault::api::ApiError(409, "qb_source_unavailable", "source directory unavailable");
        });
    drogon::app().addListener("127.0.0.1", 18860);
    std::thread server([] { drogon::app().run(); });
    struct Stop { std::thread& thread; ~Stop() { drogon::app().quit(); thread.join(); } } stop{server};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18860");
    const auto send = [&](drogon::HttpMethod method, const std::string& path,
                          const Json::Value& body = Json::Value(), std::size_t padding = 0) {
        auto request = body.isNull() ? drogon::HttpRequest::newHttpRequest()
                                     : drogon::HttpRequest::newHttpJsonRequest(body);
        request->setMethod(method); request->setPath(path);
        request->addHeader("X-Request-Id", "ignored");
        if (padding) request->setBody(std::string(padding, ' '));
        const auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        REQUIRE(response->getJsonObject());
        REQUIRE_FALSE(response->getJsonObject()->isMember("requestId"));
        REQUIRE(response->getHeader("X-Request-Id").empty());
        return response;
    };
    auto status = send(drogon::Get, "/api/qb/status");
    REQUIRE(status->statusCode() == drogon::k200OK);
    REQUIRE_FALSE((*status->getJsonObject())["configured"].asBool());
    auto rss = send(drogon::Get, "/api/qb/rss");
    REQUIRE(rss->statusCode() == drogon::k200OK);
    REQUIRE((*rss->getJsonObject())["feeds"].isArray());
    REQUIRE((*rss->getJsonObject())["errorCode"] == "qb_unconfigured");
    Json::Value feed; feed["url"] = "http://invalid.example/rss";
    auto invalid = send(drogon::Post, "/api/qb/rss/feeds", feed);
    REQUIRE(invalid->statusCode() == drogon::k400BadRequest);
    REQUIRE((*invalid->getJsonObject())["error"]["code"] == "invalid_mikan_feed_url");
    REQUIRE(send(drogon::Post, "/api/qb/rss/feeds", feed, 2049)->statusCode() == drogon::k400BadRequest);
    REQUIRE(validations.load() == 0);
    feed["url"] = "https://mikanani.me/RSS/Bangumi?bangumiId=123";
    setDirectoryError("qb_download_directory_restart_required");
    auto directory = send(drogon::Post, "/api/qb/rss/feeds", feed);
    REQUIRE(directory->statusCode() == drogon::k409Conflict);
    REQUIRE((*directory->getJsonObject())["error"]["code"] == "qb_download_directory_restart_required");
    REQUIRE(validations.load() == 0);
    Json::Value rule; rule["ruleName"] = "Local"; rule["feedUrl"] = feed["url"]; rule["keyword"] = "Local";
    auto invalidRule = rule; invalidRule["extra"] = true;
    REQUIRE(send(drogon::Post, "/api/qb/rss/rules", invalidRule)->statusCode() == drogon::k400BadRequest);
    REQUIRE(send(drogon::Post, "/api/qb/rss/rules", rule, 4097)->statusCode() == drogon::k400BadRequest);
    REQUIRE(send(drogon::Post, "/api/qb/rss/rules", rule)->statusCode() == drogon::k409Conflict);
    setDirectoryError(""); sourceReady = false;
    for (const auto* path : {"/api/qb/rss/feeds", "/api/qb/rss/rules"}) {
        auto response = send(drogon::Post, path, std::string(path).ends_with("feeds") ? feed : rule);
        REQUIRE(response->statusCode() == drogon::k409Conflict);
        REQUIRE((*response->getJsonObject())["error"]["message"] == "source directory unavailable");
    }
    neutralFailure = true;
    for (const auto* path : {"/api/qb/rss/feeds", "/api/qb/rss/rules"}) {
        auto response = send(drogon::Post, path, std::string(path).ends_with("feeds") ? feed : rule);
        REQUIRE(response->statusCode() == drogon::k409Conflict);
        REQUIRE((*response->getJsonObject())["error"]["code"] == "qb_source_unavailable");
        REQUIRE((*response->getJsonObject())["error"]["message"] == "source directory unavailable");
    }
    sourceReady = true;
    auto offlineFeed = send(drogon::Post, "/api/qb/rss/feeds", feed);
    REQUIRE(offlineFeed->statusCode() == drogon::k503ServiceUnavailable);
    REQUIRE_FALSE((*offlineFeed->getJsonObject())["success"].asBool());
    REQUIRE((*offlineFeed->getJsonObject())["error"]["message"] == "qB RSS feed action failed");
    auto offlineRule = send(drogon::Post, "/api/qb/rss/rules", rule);
    REQUIRE(offlineRule->statusCode() == drogon::k503ServiceUnavailable);
    REQUIRE((*offlineRule->getJsonObject())["error"]["message"] == "qB RSS rule action failed");
    REQUIRE_FALSE(std::filesystem::exists(data));
}
