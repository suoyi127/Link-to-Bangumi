#include "anime_vault/infrastructure/network/SteamClient.hpp"
#include "anime_vault/domain/Game.hpp"
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <chrono>
#include <cwctype>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

using namespace anime_vault;
namespace {
SteamConfig config() { return {"", "76561198000000000", std::string(32, 'a')}; }
std::map<std::string, SteamStats> fetch(std::deque<std::pair<int,std::string>> responses,
                                      std::vector<std::string> ids = {"10"}) {
    std::map<std::string,SteamStats> result;
    int completions = 0;
    SteamClient client([&](std::string, auto callback) {
        REQUIRE_FALSE(responses.empty());
        auto response = responses.front(); responses.pop_front();
        callback(response.first, response.second);
    });
    client.fetch(config(), std::move(ids), [&](auto value) { result = std::move(value); ++completions; });
    REQUIRE(completions == 1);
    REQUIRE(responses.empty());
    return result;
}
}
TEST_CASE("Steam configuration validates credentials without storing secrets") {
    REQUIRE_NOTHROW(SteamConfiguration::validate(config()));
    REQUIRE_NOTHROW(SteamConfiguration::validate({}));
    auto value = config(); value.apiKey = "invalid";
    REQUIRE_THROWS_AS(SteamConfiguration::validate(value), GameError);
    value = config(); value.steamId = "7656119800000000x";
    REQUIRE_THROWS_AS(SteamConfiguration::validate(value), GameError);
    value = config(); value.steamPath = std::string("a\0b", 3);
    REQUIRE_THROWS_AS(SteamConfiguration::validate(value), GameError);
}
TEST_CASE("Steam statistics combine owned time and player achievements") {
    auto result = fetch({{200,R"({"response":{"games":[{"appid":10,"playtime_forever":123}]}})"},
        {200,R"({"playerstats":{"success":true,"achievements":[{"achieved":1},{"achieved":0}]}})"}});
    REQUIRE(result.at("10").playtimeMinutes == 123);
    REQUIRE(result.at("10").achievementsUnlocked == 1);
    REQUIRE(result.at("10").achievementsTotal == 2);
    REQUIRE(result.at("10").errorCode.empty());
}
TEST_CASE("Steam private player statistics retain schema total and nullable personal values") {
    auto result = fetch({{200,R"({"response":{}})"}, {403,"private"},
        {200,R"({"game":{"availableGameStats":{"achievements":[{},{}]}}})"}});
    REQUIRE_FALSE(result.at("10").playtimeMinutes);
    REQUIRE_FALSE(result.at("10").achievementsUnlocked);
    REQUIRE(result.at("10").achievementsTotal == 2);
    REQUIRE(result.at("10").errorCode == "steam_stats_unavailable");
}
TEST_CASE("Steam missing owned app is not zero hours and malformed player counts remain unknown") {
    auto result = fetch({{200,R"({"response":{"games":[]}})"},
        {200,R"({"playerstats":{"success":true,"achievements":[{"achieved":"1"}]}})"},
        {200,"not json"}});
    REQUIRE_FALSE(result.at("10").playtimeMinutes);
    REQUIRE_FALSE(result.at("10").achievementsUnlocked);
    REQUIRE_FALSE(result.at("10").achievementsTotal);
}
TEST_CASE("Steam rate limits stop the batch without losing earlier time") {
    auto result = fetch({{200,R"({"response":{"games":[{"appid":10,"playtime_forever":15}]}})"},
        {429,"rate limited"}}, {"10","20"});
    REQUIRE(result.at("10").playtimeMinutes == 15);
    REQUIRE(result.at("10").errorCode == "steam_rate_limited");
    REQUIRE(result.at("20").errorCode == "steam_rate_limited");
}
TEST_CASE("Steam per-game server errors fall back to schema and continue the batch") {
    for (const auto schemaStatus : {200, 500}) {
        auto result = fetch({
            {200,R"({"response":{"games":[{"appid":10,"playtime_forever":15},{"appid":20,"playtime_forever":25}]}})"},
            {500,R"({"playerstats":{"success":false,"error":"Internal server error"}})"},
            {schemaStatus,R"({"game":{"availableGameStats":{}}})"},
            {200,R"({"playerstats":{"success":true,"achievements":[{"achieved":1}]}})"}
        }, {"10", "20"});
        REQUIRE(result.at("10").playtimeMinutes == 15);
        if (schemaStatus == 200) {
            REQUIRE(result.at("10").achievementsTotal == 0);
            REQUIRE(result.at("10").achievementsUnlocked == 0);
            REQUIRE(result.at("10").errorCode.empty());
        } else {
            REQUIRE_FALSE(result.at("10").achievementsTotal);
            REQUIRE(result.at("10").errorCode == "steam_stats_unavailable");
        }
        REQUIRE(result.at("20").playtimeMinutes == 25);
        REQUIRE(result.at("20").achievementsUnlocked == 1);
        REQUIRE(result.at("20").achievementsTotal == 1);
        REQUIRE(result.at("20").errorCode.empty());
    }
}
TEST_CASE("Steam explicitly unsupported statistics are distinct from connection failures") {
    auto result = fetch({{200,R"({"response":{"games":[{"appid":10,"playtime_forever":15}]}})"},
        {400,R"({"playerstats":{"success":false,"error":"Requested app has no stats"}})"},
        {200,R"({"game":{}})"}});
    REQUIRE(result.at("10").playtimeMinutes == 15);
    REQUIRE_FALSE(result.at("10").achievementsTotal);
    REQUIRE_FALSE(result.at("10").achievementsUnlocked);
    REQUIRE(result.at("10").errorCode == "steam_stats_not_supported");
}
TEST_CASE("Steam owned games server errors stop the batch") {
    auto result = fetch({{503,"unavailable"}}, {"10", "20"});
    REQUIRE(result.at("10").errorCode == "steam_stats_unavailable");
    REQUIRE(result.at("20").errorCode == "steam_stats_unavailable");
}
TEST_CASE("Steam successful empty schema is authoritative zero total") {
    auto result = fetch({{200,"{}"},{200,R"({"playerstats":{"success":false}})"},
        {200,R"({"game":{"availableGameStats":{}}})"}});
    REQUIRE(result.at("10").achievementsTotal == 0);
    REQUIRE(result.at("10").achievementsUnlocked == 0);
}
TEST_CASE("Steam named versioned schemas without stats confirm no achievements") {
    for (const auto& schema : {
        R"({"game":{"gameName":"No achievements","gameVersion":1}})",
        R"({"game":{"gameName":"No achievements","gameVersion":"1"}})"}) {
        auto result = fetch({{200,"{}"},{403,"private"},{200,schema}});
        REQUIRE(result.at("10").achievementsTotal == 0);
        REQUIRE(result.at("10").achievementsUnlocked == 0);
    }
}
TEST_CASE("Steam incomplete or malformed schemas never manufacture zero achievements") {
    for (const auto& schema : {R"({})",R"({"game":{}})",
        R"({"game":{"gameName":"Game","gameVersion":null}})",
        R"({"game":{"gameName":"Game","gameVersion":1,"availableGameStats":null}})"}) {
        auto result = fetch({{200,"{}"},{403,"private"},{200,schema}});
        REQUIRE_FALSE(result.at("10").achievementsTotal);
        REQUIRE_FALSE(result.at("10").achievementsUnlocked);
    }
}
TEST_CASE("Steam missing credentials never issue requests") {
    SteamClient client([](std::string,auto) { FAIL("unexpected network request"); });
    client.fetch({}, {"10"}, [](auto result) {
        REQUIRE(result.at("10").errorCode == "steam_credentials_required");
    });
}
TEST_CASE("Steam transport errors stop requests for all remaining apps") {
    auto result = fetch({{0,""}}, {"10","20","30"});
    REQUIRE(result.size() == 3);
    for (const auto& [id, stats] : result) {
        REQUIRE(stats.errorCode == "steam_stats_unavailable");
        REQUIRE_FALSE(stats.playtimeMinutes);
        REQUIRE_FALSE(stats.achievementsTotal);
    }
}
TEST_CASE("Steam schema failure preserves successful personal fields") {
    auto result = fetch({{200,R"({"response":{"games":[{"appid":10,"playtime_forever":0}]}})"},
        {200,R"({"playerstats":{"success":true,"achievements":[]}})"}, {200,"{}"}});
    REQUIRE(result.at("10").playtimeMinutes == 0);
    REQUIRE(result.at("10").achievementsUnlocked == 0);
    REQUIRE_FALSE(result.at("10").achievementsTotal);
}
#ifdef _WIN32
TEST_CASE("Steam credentials round trip in an isolated Windows target") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-steam-test-" + std::to_string(GetCurrentProcessId()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto normalized = std::filesystem::absolute(path).lexically_normal().wstring();
    std::transform(normalized.begin(),normalized.end(),normalized.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    struct Cleanup {
        std::wstring target;
        ~Cleanup() { CredDeleteW(target.c_str(),CRED_TYPE_GENERIC,0); }
    } cleanup{L"AnimeVault/steam/" + normalized};
    SteamConfiguration original(path);
    REQUIRE(original.current().apiKey.empty());
    auto value = config(); value.steamPath = "C:\\Steam";
    original.save(value);
    SteamConfiguration reloaded(path);
    REQUIRE(reloaded.current().apiKey == value.apiKey);
    REQUIRE(reloaded.current().steamPath == value.steamPath);
    value.apiKey.clear(); value.steamPath = "D:\\Steam";
    reloaded.save(value);
    SteamConfiguration retained(path);
    REQUIRE(retained.current().apiKey == config().apiKey);
    REQUIRE(retained.current().steamPath == "D:\\Steam");
    REQUIRE_FALSE(std::filesystem::exists(path));
    value.apiKey = "bad";
    REQUIRE_THROWS_AS(retained.save(value), GameError);
    REQUIRE(retained.current().apiKey == config().apiKey);
}
#endif
