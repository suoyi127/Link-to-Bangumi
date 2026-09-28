#include "anime_vault/api/BangumiConfigController.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"

#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>
#include <filesystem>
#include <random>
#include <thread>

TEST_CASE("Bangumi configuration API validates and switches the public User-Agent") {
    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path() /
        ("anime-vault-bangumi-http-" + std::to_string(std::random_device{}()) + ".db");
    auto db = std::make_shared<anime_vault::SqliteDatabase>(path);
    db->migrate();
    auto manager = std::make_shared<anime_vault::BangumiConnectionManager>(*db, "");
    anime_vault::api::registerBangumiConfigEndpoints(manager);
    drogon::app().addListener("127.0.0.1", 18852);
    std::thread server([] { drogon::app().run(); });
    struct Stop { std::thread& thread; ~Stop() { drogon::app().quit(); thread.join(); } } stop{server};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18852");
    const auto send = [&](drogon::HttpMethod method, const Json::Value& body = Json::Value()) {
        auto request = body.isNull() ? drogon::HttpRequest::newHttpRequest()
                                     : drogon::HttpRequest::newHttpJsonRequest(body);
        request->setMethod(method); request->setPath("/api/bangumi/config");
        auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response;
    };
    REQUIRE((*send(drogon::Get)->getJsonObject())["source"].asString() == "none");
    Json::Value body;
    body["userAgent"] = "suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)";
    REQUIRE((*send(drogon::Put, body)->getJsonObject())["source"].asString() == "saved");
    REQUIRE(manager->summary().configured);
    body["userAgent"] = "bad\r\nInjected: true";
    REQUIRE((*send(drogon::Put, body)->getJsonObject())["error"]["code"].asString() == "invalid_user_agent");
    REQUIRE((*send(drogon::Get)->getJsonObject())["source"].asString() == "saved");
    REQUIRE((*send(drogon::Delete)->getJsonObject())["source"].asString() == "none");
}
