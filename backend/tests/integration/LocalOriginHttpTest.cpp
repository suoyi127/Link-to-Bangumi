#include "anime_vault/api/GameController.hpp"
#include "anime_vault/api/NovelController.hpp"
#include <catch2/catch_test_macros.hpp>
#include <drogon/drogon.h>
#include <thread>

TEST_CASE("desktop dynamic origin reaches input validation while foreign origins are rejected") {
    anime_vault::NativeProcessLauncher launcher;
    anime_vault::api::registerGameEndpoints(nullptr, nullptr, launcher);
    anime_vault::api::registerNovelEndpoints(nullptr, nullptr, launcher);
    drogon::app().addListener("127.0.0.1", 18854);
    std::thread server([] { drogon::app().run(); });
    struct Stop { std::thread& thread; ~Stop() { drogon::app().quit(); thread.join(); } } stop{server};
    for (int i = 0; i < 100 && !drogon::app().isRunning(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(drogon::app().isRunning());
    auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:18854");
    const auto send = [&](const std::string& path, const std::string& origin, const std::string& site) {
        // 使用无效字段停在输入校验，不读取磁盘或调用导入服务。
        Json::Value body; body["unexpected"] = true;
        auto request = drogon::HttpRequest::newHttpJsonRequest(body);
        request->setMethod(drogon::Post); request->setPath(path);
        if (!origin.empty()) request->addHeader("Origin", origin);
        request->addHeader("Sec-Fetch-Site", site);
        auto [result, response] = client->sendRequest(request);
        REQUIRE(result == drogon::ReqResult::Ok);
        REQUIRE(response);
        return (*response->getJsonObject())["error"]["code"].asString();
    };
    for (const auto& path : {"/api/game-imports", "/api/novel-imports"}) {
        REQUIRE(send(path, "http://127.0.0.1:18854", "same-origin") == "invalid_request");
        REQUIRE(send(path, "http://localhost:18854", "same-origin") == "invalid_request");
        REQUIRE(send(path, "http://localhost:5173", "same-site") == "invalid_request");
        REQUIRE(send(path, "https://evil.example", "same-origin") == "cross_site_request_forbidden");
        REQUIRE(send(path, "http://127.0.0.1:18855", "same-site") == "cross_site_request_forbidden");
        REQUIRE(send(path, "http://127.0.0.1:18854", "cross-site") == "cross_site_request_forbidden");
    }
}
