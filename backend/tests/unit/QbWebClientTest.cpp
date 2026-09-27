#include "anime_vault/infrastructure/network/QbWebClient.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <regex>

using anime_vault::QbWebClient;

TEST_CASE("qB 5.2 login accepts 204 with its port-specific session cookie") {
    REQUIRE(QbWebClient::loginCookie(204, "", "QBT_SID_8080=opaque; Path=/; HttpOnly") ==
        "QBT_SID_8080=opaque");
}

TEST_CASE("qB legacy login accepts 200 Ok with SID cookie") {
    REQUIRE(QbWebClient::loginCookie(200, "Ok.", "SID=opaque; HttpOnly") ==
        "SID=opaque");
}

TEST_CASE("qB login reads cookies parsed by Drogon rather than only raw headers") {
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    response->addCookie("QBT_SID_8080", "opaque");
    REQUIRE(QbWebClient::loginCookie(response) == "QBT_SID_8080=opaque");
}

TEST_CASE("qB login does not accept unrelated cookies or failed responses") {
    REQUIRE_FALSE(QbWebClient::loginCookie(204, "", "SESSION=opaque; Path=/"));
    REQUIRE_FALSE(QbWebClient::loginCookie(403, "", "QBT_SID_8080=opaque"));
    REQUIRE_FALSE(QbWebClient::loginCookie(200, "Fails.", "SID=opaque"));
}

TEST_CASE("qB RSS catalog extracts only Mikan bilingual article mappings") {
    const auto result = QbWebClient::parseMikanItems(R"({
      "Mikan Project": {"url":"https://mikanani.me/RSS/Bangumi?bangumiId=1",
        "articles":[{"title":"[桜都字幕组] 中文标题 / Roman Title. [03][1080P]"},
                    {"title":"[桜都字幕组] 中文标题 / Roman Title. [04][1080P]"}]},
      "Other": {"url":"https://example.com/rss",
        "articles":[{"title":"[Group] Wrong / Roman Title [03][1080P]"}]}
    })");
    REQUIRE(result.feedCount == 1);
    REQUIRE(result.articleCount == 2);
    REQUIRE(result.pairs.size() == 1);
    REQUIRE(result.pairs[0].canonicalTitle == "中文标题");
    REQUIRE(result.pairs[0].alias == "Roman Title");
    REQUIRE(result.feeds.size() == 1);
    REQUIRE(result.feeds[0].articleCount == 2);
}

TEST_CASE("qB RSS catalog maps a three-language dashed Mikan title") {
    const auto catalog = QbWebClient::parseMikanItems(R"({
      "Mikan Project": {"url":"https://mikanani.me/RSS/Bangumi?bangumiId=3985&subgroupid=1208",
        "articles":[{"title":"[三明治摆烂组] 才女的侍从 / Saijo no Osewa / 才女のお世话 - 08 - [简日内嵌][AVC 8bit 1080P]"}]}
    })");
    REQUIRE(catalog.feedCount == 1);
    REQUIRE(catalog.pairs.size() == 1);
    REQUIRE(catalog.pairs[0].alias == "Saijo no Osewa");
    REQUIRE(catalog.pairs[0].episode == 8);
}

TEST_CASE("qB feed addition accepts only bounded Mikan HTTPS URLs") {
    REQUIRE(QbWebClient::validMikanFeedUrl("https://mikanani.me/RSS/Bangumi?bangumiId=123"));
    REQUIRE_FALSE(QbWebClient::validMikanFeedUrl("http://mikanani.me/RSS/Bangumi?bangumiId=123"));
    REQUIRE_FALSE(QbWebClient::validMikanFeedUrl("https://mikanani.me.evil.test/RSS/Bangumi?bangumiId=123"));
    REQUIRE_FALSE(QbWebClient::validMikanFeedUrl("https://mikanani.me/RSS/Bangumi?bangumiId=123#fragment"));
    REQUIRE_FALSE(QbWebClient::validMikanFeedUrl("https://[::1]:8080/RSS/Bangumi?bangumiId=123"));
}

TEST_CASE("qB 5.2 feed addition includes its required path parameter") {
    const std::string url = "https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203";
    REQUIRE(QbWebClient::mikanFeedForm(url) ==
        "url=https%3A%2F%2Fmikanani.me%2FRSS%2FBangumi%3FbangumiId%3D4011%26subgroupid%3D203"
        "&path=https%3A%2F%2Fmikanani.me%2FRSS%2FBangumi%3FbangumiId%3D4011%26subgroupid%3D203");
}

TEST_CASE("qB RSS catalog reports unconfigured credentials without a network request") {
    QbWebClient client("http://[::1]:8080", "", "");
    bool completed = false;
    client.readMikanTitles([&](anime_vault::QbMikanCatalog result) {
        completed = true;
        REQUIRE(result.errorCode == "qb_unconfigured");
    });
    REQUIRE(completed);
}

TEST_CASE("qB refuses an invalid feed before any network request") {
    QbWebClient client("http://[::1]:8080", "", "");
    bool completed = false;
    client.addMikanFeed("http://127.0.0.1:8080/private", [&](anime_vault::QbActionResult result) {
        completed = true;
        REQUIRE_FALSE(result.success);
        REQUIRE(result.errorCode == "invalid_mikan_feed_url");
    });
    REQUIRE(completed);
}

TEST_CASE("qB auto-download rule is restricted to one Mikan feed and source directory") {
    const anime_vault::QbMikanRuleSpec spec{
        "AnimeVault Transparent Night", "https://mikanani.me/RSS/Bangumi?bangumiId=123",
        "桜都字幕组", "D:/追番/番剧"};
    const auto rule = nlohmann::json::parse(QbWebClient::mikanRuleJson(spec));
    REQUIRE(rule["enabled"] == true);
    REQUIRE(rule["mustContain"] == "桜都字幕组");
    REQUIRE(rule["affectedFeeds"].size() == 1);
    REQUIRE(rule["affectedFeeds"][0] == spec.feedUrl);
    REQUIRE(rule["savePath"] == spec.savePath);
    REQUIRE(rule["smartFilter"] == false);
}

TEST_CASE("qB refuses an invalid auto-download rule before a network request") {
    QbWebClient client("http://[::1]:8080", "", "");
    bool completed = false;
    client.createMikanRule({"test", "https://example.com/rss", "keyword", "D:/source"},
        [&](anime_vault::QbActionResult result) {
            completed = true;
            REQUIRE_FALSE(result.success);
            REQUIRE(result.errorCode == "invalid_mikan_rule");
        });
    REQUIRE(completed);
}

TEST_CASE("automatic Mikan rule accepts the first recognized resource per episode") {
    const anime_vault::QbMikanRuleSpec spec{
        "AnimeVault Auto 4011", "https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203",
        "", "D:/追番/番剧"};
    const auto rule = nlohmann::json::parse(QbWebClient::autoMikanRuleJson(spec));
    REQUIRE(rule["enabled"] == true);
    REQUIRE(rule["mustContain"] == "");
    REQUIRE(rule["smartFilter"] == true);
    REQUIRE(rule["affectedFeeds"] == nlohmann::json::array({spec.feedUrl}));
    REQUIRE(rule["savePath"] == spec.savePath);
    const auto pending = nlohmann::json::parse(QbWebClient::autoMikanRuleJson(spec, false));
    REQUIRE(pending["enabled"] == false);
    REQUIRE(pending["affectedFeeds"] == rule["affectedFeeds"]);
}

TEST_CASE("automatic episode pattern normalizes zero-padded bracket episodes") {
    const std::regex pattern("(?:_|\\b)(?:" + QbWebClient::bracketEpisodePattern() + ")(?:_|\\b)");
    const auto episode = [&](const std::string& title) {
        std::smatch match;
        return std::regex_search(title, match, pattern) ? match[1].str() : std::string{};
    };
    REQUIRE(episode("[Group] Anime Name [01][1080P][CHS]") == "1");
    REQUIRE(episode("[Group] Anime Name [1][1080P][CHS]") == "1");
    REQUIRE(episode("[Group] Anime Name [001][1080P][CHS]") == "1");
    REQUIRE(episode("[Group] Anime Name [1080P][CHS]").empty());
}

TEST_CASE("automatic episode pattern recognizes dashed Mikan episodes") {
    const std::regex pattern("(?:_|\\b)(?:" + QbWebClient::dashedEpisodePattern() + ")(?:_|\\b)");
    std::smatch match;
    const std::string title = "[Group] Saijo no Osewa - 08 - [1080P][CHS]";
    REQUIRE(std::regex_search(title, match, pattern));
    REQUIRE(match[1].str() == "8");
    REQUIRE_FALSE(std::regex_search(std::string("[Group] Saijo no Osewa - 00 - [1080P]"), pattern));
}

TEST_CASE("automatic rule waits for a fetched feed before marking old articles read") {
    const std::string url = "https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203";
    REQUIRE_FALSE(QbWebClient::readyMikanFeedPath(R"({"loading":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203","isLoading":true,"hasError":false,"articles":[]}})", url));
    REQUIRE(QbWebClient::readyMikanFeedPath(R"({"Mikan Project - Anime":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203","isLoading":false,"hasError":false,"articles":[{"title":"Anime [01]"}]}})", url) == "Mikan Project - Anime");
}

TEST_CASE("automatic episode preferences append to qB's newline-delimited filters") {
    const auto settings = nlohmann::json::parse(QbWebClient::autoEpisodePreferencesJson(
        R"json({"rss_auto_downloading_enabled":true,"rss_processing_enabled":true,"rss_download_repack_proper_episodes":true,"rss_smart_episode_filters":"s(\\d+)e(\\d+)\n(\\d+)x(\\d+)"})json"));
    REQUIRE(settings["rss_download_repack_proper_episodes"] == false);
    REQUIRE(settings["rss_smart_episode_filters"].is_string());
    REQUIRE(settings["rss_smart_episode_filters"].get<std::string>() ==
        "s(\\d+)e(\\d+)\n(\\d+)x(\\d+)\n" + QbWebClient::bracketEpisodePattern() +
        "\n" + QbWebClient::dashedEpisodePattern());
}
