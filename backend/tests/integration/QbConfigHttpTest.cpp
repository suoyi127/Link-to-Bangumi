#include "anime_vault/api/QbConfigController.hpp"
#include "anime_vault/infrastructure/network/QbConnectionManager.hpp"

#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>
#include <filesystem>
#include <random>
#include <thread>

namespace fs = std::filesystem;

TEST_CASE("qB configuration API saves and clears credentials without returning passwords") {
    const auto data = fs::temp_directory_path() /
        ("anime-vault-qb-http-" + std::to_string(std::random_device{}()));
    auto manager = std::make_shared<anime_vault::QbConnectionManager>(data,
        anime_vault::QbConnectionConfig{"http://[::1]:8080", "", ""});
    struct Cleanup { std::shared_ptr<anime_vault::QbConnectionManager> manager;
        ~Cleanup() { manager->clear(); } } cleanup{manager};
    anime_vault::api::registerQbConfigEndpoints(manager);
    drogon::app().addListener("127.0.0.1", 18851);
    std::thread server([] { drogon::app().run(); });
    struct Stop { std::thread& thread; ~Stop() { drogon::app().quit(); thread.join(); } } stop{server};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18851");
    const auto send = [&](drogon::HttpMethod method, const std::string& path,
                          const Json::Value& body = Json::Value()) {
        auto request = body.isNull() ? drogon::HttpRequest::newHttpRequest()
                                     : drogon::HttpRequest::newHttpJsonRequest(body);
        request->setMethod(method); request->setPath(path);
        auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return response;
    };
    auto initial = send(drogon::Get, "/api/qb/config");
    REQUIRE((*initial->getJsonObject())["source"].asString() == "none");
    Json::Value body;
    body["url"] = "http://127.0.0.1:18851";
    body["username"] = "test-user";
    body["password"] = "test-secret";
    const auto saved = send(drogon::Put, "/api/qb/config", body);
    REQUIRE(saved->statusCode() == drogon::k200OK);
    REQUIRE(saved->getBody().find("test-secret") == std::string::npos);
    REQUIRE((*saved->getJsonObject())["source"].asString() == "saved");
    REQUIRE(manager->current()->baseUrl() == "http://127.0.0.1:18851");
    REQUIRE(send(drogon::Get, "/api/qb/config")->getBody().find("test-secret") == std::string::npos);
    body["password"] = "";
    REQUIRE(send(drogon::Put, "/api/qb/config", body)->statusCode() == drogon::k200OK);
    auto test = send(drogon::Post, "/api/qb/config/test", body);
    REQUIRE((*test->getJsonObject())["errorCode"].asString() == "qb_unexpected_service");
    REQUIRE(test->getBody().find("test-secret") == std::string::npos);
    body["url"] = "http://remote.example:8080";
    REQUIRE((*send(drogon::Put, "/api/qb/config", body)->getJsonObject())["error"]["code"].asString() ==
        "invalid_qb_webui_url");
    REQUIRE(send(drogon::Delete, "/api/qb/config")->statusCode() == drogon::k200OK);
    REQUIRE((*send(drogon::Get, "/api/qb/config")->getJsonObject())["source"].asString() == "none");
}
