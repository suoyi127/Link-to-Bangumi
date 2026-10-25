#include "anime_vault/infrastructure/network/QbWebClient.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <regex>
#include <filesystem>

using anime_vault::QbWebClient;

TEST_CASE("qB endpoint accepts only explicit HTTP loopback addresses and ports") {
    const auto ipv6 = QbWebClient::parseEndpoint("http://[::1]:8080");
    REQUIRE(ipv6.address == "::1");
    REQUIRE(ipv6.port == 8080);
    REQUIRE(ipv6.hostHeader == "[::1]:8080");
    const auto ipv4 = QbWebClient::parseEndpoint("http://127.0.0.1:8090");
    REQUIRE(ipv4.address == "127.0.0.1");
    REQUIRE(ipv4.port == 8090);
    REQUIRE_THROWS(QbWebClient::parseEndpoint("http://localhost:8080"));
    REQUIRE_THROWS(QbWebClient::parseEndpoint("http://192.168.1.2:8080"));
    REQUIRE_THROWS(QbWebClient::parseEndpoint("http://user:pass@127.0.0.1:8080"));
    REQUIRE_THROWS(QbWebClient::parseEndpoint("https://[::1]:8080"));
    REQUIRE_THROWS(QbWebClient::parseEndpoint("http://[::1]:8080/path"));
    REQUIRE_THROWS(QbWebClient::parseEndpoint("http://[::1]:8080?x=1"));
    REQUIRE_THROWS(QbWebClient::parseEndpoint("http://[::1]:0"));
    REQUIRE_THROWS(QbWebClient::parseEndpoint("http://127.0.0.1:65536"));
}

TEST_CASE("qB landing-page check rejects unrelated local services before login") {
    REQUIRE(QbWebClient::looksLikeQbWebUi("<meta name=\"description\" content=\"qBittorrent WebUI\">"));
    REQUIRE_FALSE(QbWebClient::looksLikeQbWebUi("<html>another local server</html>"));
}

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
    REQUIRE(catalog.pairs.size() == 2);
    bool foundRoman = false;
    for (const auto& pair : catalog.pairs) {
        foundRoman = foundRoman || pair.alias == "Saijo no Osewa";
        CHECK(pair.episode == 8);
    }
    CHECK(foundRoman);
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
    REQUIRE_FALSE(rule["mustContain"].get<std::string>().empty());
    REQUIRE(rule["useRegex"] == true);
    REQUIRE(rule["mustNotContain"] == "REPACK");
    REQUIRE(rule["addPaused"] == false);
    REQUIRE(rule["ignoreDays"] == 0);
    REQUIRE(rule["smartFilter"] == true);
    REQUIRE(rule["affectedFeeds"] == nlohmann::json::array({spec.feedUrl}));
    REQUIRE(rule["savePath"] == spec.savePath);
    const auto pending = nlohmann::json::parse(QbWebClient::autoMikanRuleJson(spec, false));
    REQUIRE(pending["enabled"] == false);
    REQUIRE(pending["affectedFeeds"] == rule["affectedFeeds"]);
}

TEST_CASE("automatic rule waits for a fetched feed before marking old articles read") {
    const std::string url = "https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203";
    REQUIRE_FALSE(QbWebClient::readyMikanFeedPath(R"({"loading":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203","isLoading":true,"hasError":false,"articles":[]}})", url));
    REQUIRE(QbWebClient::readyMikanFeedPath(R"({"Mikan Project - Anime":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203","isLoading":false,"hasError":false,"articles":[{"title":"Anime [01]"}]}})", url) == "Mikan Project - Anime");
}

TEST_CASE("automatic episode preferences replace unsafe defaults with five episode-only filters") {
    const auto settings = nlohmann::json::parse(QbWebClient::autoEpisodePreferencesJson(
        R"({"rss_smart_episode_filters":"old unsafe filter"})"));
    REQUIRE(settings["rss_download_repack_proper_episodes"] == false);
    const auto filters = settings["rss_smart_episode_filters"].get<std::string>();
    REQUIRE(std::count(filters.begin(), filters.end(), '\n') == 4);
    REQUIRE(filters.find("old unsafe filter") == std::string::npos);
    REQUIRE(filters.find(R"(\bs\d{1,2}e0*(\d{1,3})(?:v\d+)?\b)") != std::string::npos);
    REQUIRE(nlohmann::json::parse(QbWebClient::autoEpisodePreferencesJson(settings.dump())) == settings);
}

TEST_CASE("completed qB paths exclude unfinished checking and unsafe paths") {
    auto data = nlohmann::json::parse(R"([
      {"progress":1,"state":"uploading","content_path":"D:/downloads/Ready"},
      {"progress":0.5,"state":"downloading","content_path":"D:/downloads/Partial"},
      {"progress":1,"state":"checkingUP","content_path":"D:/downloads/Checking"},
      {"progress":1,"state":"error","content_path":"D:/downloads/Error"},
      {"progress":1,"state":"pausedUP","content_path":"../outside"}
    ])");
    const auto ready = (std::filesystem::temp_directory_path() / "Ready").generic_string();
    data[0]["content_path"] = ready;
    const auto paths = anime_vault::QbWebClient::completedPaths(data.dump());
    REQUIRE(paths.size() == 1);
    CHECK(paths.front() == ready);
}

TEST_CASE("qB preserves multiple Mikan aliases and rejects normalized alias conflicts") {
    const auto catalog = QbWebClient::parseMikanItems(R"({"feed":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=1","articles":[
        {"title":"[G] 安妮的童话 / 安妮·雪莉 / Anne Shirley - 17 [1080P]"},
        {"title":"[G] 中文甲 / Shared Name [01][1080P]"},
        {"title":"[G] 中文乙 / Shared_Name [02][1080P]"}
    ]}})");
    REQUIRE(catalog.pairs.size() == 2);
    for (const auto& pair : catalog.pairs) CHECK(pair.canonicalTitle == "安妮的童话");
}

TEST_CASE("single anime Mikan feeds resolve the four local unmatched releases") {
    const auto catalog = QbWebClient::parseMikanItems(R"({
      "rail":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4086","title":"Mikan Project - 铁旅！邂逅铁道少女","articles":[{"title":"[ANi] 铁旅！meet with 铁道少女 - 01 [1080P][Baha]"}]},
      "witch":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4092","title":"Mikan Project - 你好，我是受心上人所托来做恋爱药的魔女。","articles":[{"title":"[G] 你好，身为魔女的我，被心上人委托制作迷情药 / Horemajo - 01 [1080P]"}]},
      "fx":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4078","title":"Mikan Project - FX战士久留美","articles":[{"title":"[G] FX 战士久留美 / FX Senshi Kurumi-chan - 01 [1080P]"}]},
      "apothecary":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4106","title":"Mikan Project - 药屋少女的呢喃 第三季","articles":[{"title":"【喵萌奶茶屋】★10月新番★[药师少女的独语 / 药屋少女的呢喃 / Kusuriya no Hitorigoto][49][1080p][简日双语]"}]}
    })");
    const auto matches = [&](const std::string& alias, const std::string& canonical) {
        return std::any_of(catalog.pairs.begin(), catalog.pairs.end(), [&](const auto& pair) {
            return anime_vault::normalizedTitleKey(pair.alias) == anime_vault::normalizedTitleKey(alias) && pair.canonicalTitle == canonical;
        });
    };
    CHECK(matches("鐵旅！meet with 鐵道少女", "铁旅！邂逅铁道少女"));
    CHECK(matches("Horemajo", "你好，我是受心上人所托来做恋爱药的魔女。"));
    CHECK(matches("FX战士久留美 FX Senshi Kurumi-chan", "FX战士久留美"));
    CHECK(matches("Kusuriya no Hitorigoto", "药屋少女的呢喃 第三季"));
    CHECK(matches("★10月新番★[药师少女的独语", "药屋少女的呢喃 第三季"));
}

TEST_CASE("automatic history uses same-feed records and task evidence instead of read status") {
    const anime_vault::QbMikanRuleSpec spec{"auto", "https://mikanani.me/RSS/Bangumi?bangumiId=1", "", "D:/source"};
    const auto history = nlohmann::json::parse(QbWebClient::autoMikanHistoryJson(spec,
        R"json({"old":{"affectedFeeds":["https://mikanani.me/RSS/Bangumi?bangumiId=1"],"previouslyMatchedEpisodes":["001","1080","2026","1x2"]},"other":{"affectedFeeds":["other"],"previouslyMatchedEpisodes":["9"]}})json",
        R"json({"feed":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=1","articles":[{"title":"Anime [02v2]","isRead":false},{"title":"Anime - 03 (1080p)","isRead":true},{"title":"Anime [04]","isRead":true}]}})json",
        R"json([{"name":"Anime [02v2]"},{"name":"Anime - 03 (1080p)"},{"name":"Other [09]"}])json"));
    REQUIRE(history == nlohmann::json::array({"1", "2", "3"}));
}
