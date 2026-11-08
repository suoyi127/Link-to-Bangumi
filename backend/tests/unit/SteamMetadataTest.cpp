#include "anime_vault/infrastructure/network/SteamMetadataClient.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <deque>
using namespace anime_vault;
namespace {
std::string store(const std::string& title) {
    return nlohmann::json{{"10", {{"success", true}, {"data", {{"name", title}, {"header_image", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/10/header_schinese.jpg?t=1"}}}}}}.dump();
}
const std::string png = "\x89PNG\r\n\x1a\nfixture";
}
TEST_CASE("Steam metadata prefers simplified Chinese and caches official cover bytes") {
    int calls = 0;
    SteamMetadataClient client([&](std::string host, std::string path, auto done) {
        ++calls;
        REQUIRE(path.find("key=") == std::string::npos);
        if (calls == 1) {
            REQUIRE(host == "store.steampowered.com");
            REQUIRE(path == "/api/appdetails?appids=10&l=schinese");
            done(200, store("中文游戏"));
        } else { REQUIRE(host == "shared.akamai.steamstatic.com"); done(200, png); }
    });
    bool completed = false;
    client.fetch({"10"}, [&](auto result) {
        completed = true;
        REQUIRE(result.size() == 1);
        REQUIRE(result.at("10").chineseTitle == "中文游戏");
        REQUIRE(result.at("10").cover->bytes == png);
        REQUIRE(result.at("10").cover->mimeType == "image/png");
        REQUIRE(result.at("10").warnings.empty());
    });
    REQUIRE(completed); REQUIRE(calls == 2);
}
TEST_CASE("Steam metadata falls back to traditional Chinese without inventing translations") {
    for (const auto& title : {"繁體遊戲", "English Game"}) {
        int calls = 0;
        SteamMetadataClient client([&](std::string, std::string path, auto done) {
            ++calls;
            if (calls == 1) done(200, store("English Game"));
            else if (calls == 2) { REQUIRE(path.find("l=tchinese") != std::string::npos); done(200, store(title)); }
            else done(200, png);
        });
        client.fetch({"10"}, [&](auto result) {
            REQUIRE(result.size() == 1);
            REQUIRE(result.at("10").chineseTitle == (std::string(title) == "English Game" ? "" : title));
            REQUIRE(result.at("10").cover.has_value());
        });
        REQUIRE(calls == 3);
    }
}
TEST_CASE("Steam metadata rejects non-images while keeping the Chinese title") {
    int calls = 0;
    SteamMetadataClient client([&](std::string, std::string, auto done) { done(200, ++calls == 1 ? store("中文名") : "<html>error</html>"); });
    client.fetch({"10"}, [](auto result) {
        REQUIRE(result.size() == 1);
        REQUIRE(result.at("10").chineseTitle == "中文名");
        REQUIRE_FALSE(result.at("10").cover);
        REQUIRE(result.at("10").warnings == std::vector<std::string>{"steam_cover_unavailable"});
    });
}
TEST_CASE("Steam covers only use matching app assets on official HTTPS hosts") {
    REQUIRE(SteamMetadataClient::coverRequest("https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/10/abc/header.jpg?t=1", "10"));
    for (const auto& url : {"http://shared.akamai.steamstatic.com/steam/apps/10/header.jpg",
        "https://shared.akamai.steamstatic.com.evil.test/steam/apps/10/header.jpg",
        "https://127.0.0.1/steam/apps/10/header.jpg", "https://shared.akamai.steamstatic.com:443/steam/apps/10/header.jpg",
        "https://shared.akamai.steamstatic.com/steam/apps/20/header.jpg",
        "https://shared.akamai.steamstatic.com/steam/apps/10/../header.jpg"})
        REQUIRE_FALSE(SteamMetadataClient::coverRequest(url, "10"));
}
TEST_CASE("Steam metadata failures remain separate from successful subsequent games") {
    int calls = 0;
    SteamMetadataClient client([&](std::string, std::string, auto done) {
        ++calls;
        if (calls == 1) done(500, "error");
        else if (calls == 2) done(200, store("中文名"));
        else done(200, png);
    });
    client.fetch({"20", "10"}, [](auto result) {
        REQUIRE(result.size() == 2);
        REQUIRE(result.at("20").warnings == std::vector<std::string>{"steam_metadata_unavailable"});
        REQUIRE(result.at("10").cover.has_value());
    });
}
TEST_CASE("Steam metadata stops promptly on offline or rate limited transport") {
    for (const auto status : {0, 429}) {
        int calls = 0;
        SteamMetadataClient client([&](std::string, std::string, auto done) { ++calls; done(status, ""); });
        client.fetch({"10", "20"}, [](auto result) {
            REQUIRE(result.size() == 2);
            for (const auto& [id, value] : result) {
                REQUIRE_FALSE(value.cover);
                REQUIRE_FALSE(value.warnings.empty());
            }
        });
        REQUIRE(calls == 1);
    }
}
TEST_CASE("Steam metadata malformed store data never replaces existing metadata") {
    for (const auto body : {"not json", "{}", R"({"10":{"success":false}})", R"({"10":{"success":true,"data":{"name":42}}})"}) {
        SteamMetadataClient client([&](std::string, std::string, auto done) { done(200, body); });
        client.fetch({"10"}, [](auto result) {
            REQUIRE(result.size() == 1);
            REQUIRE(result.at("10").chineseTitle.empty());
            REQUIRE_FALSE(result.at("10").cover);
            REQUIRE_FALSE(result.at("10").warnings.empty());
        });
    }
}
