#include "anime_vault/services/QbRssRules.hpp"
#include "anime_vault/services/TitleNormalization.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <regex>
#include <filesystem>
#include <stdexcept>

using anime_vault::QbRssRules;

TEST_CASE("qB RSS catalog extracts only Mikan bilingual article mappings") {
    const auto result = QbRssRules::parseMikanItems(R"({
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
    const auto catalog = QbRssRules::parseMikanItems(R"({
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
    REQUIRE(QbRssRules::validMikanFeedUrl("https://mikanani.me/RSS/Bangumi?bangumiId=123"));
    REQUIRE_FALSE(QbRssRules::validMikanFeedUrl("http://mikanani.me/RSS/Bangumi?bangumiId=123"));
    REQUIRE_FALSE(QbRssRules::validMikanFeedUrl("https://mikanani.me.evil.test/RSS/Bangumi?bangumiId=123"));
    REQUIRE_FALSE(QbRssRules::validMikanFeedUrl("https://mikanani.me/RSS/Bangumi?bangumiId=123#fragment"));
    REQUIRE_FALSE(QbRssRules::validMikanFeedUrl("https://[::1]:8080/RSS/Bangumi?bangumiId=123"));
}

TEST_CASE("qB 5.2 feed addition includes its required path parameter") {
    const std::string url = "https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203";
    REQUIRE(QbRssRules::mikanFeedForm(url) ==
        "url=https%3A%2F%2Fmikanani.me%2FRSS%2FBangumi%3FbangumiId%3D4011%26subgroupid%3D203"
        "&path=https%3A%2F%2Fmikanani.me%2FRSS%2FBangumi%3FbangumiId%3D4011%26subgroupid%3D203");
}

TEST_CASE("qB auto-download rule is restricted to one Mikan feed and source directory") {
    const anime_vault::QbMikanRuleSpec spec{
        "AnimeVault Transparent Night", "https://mikanani.me/RSS/Bangumi?bangumiId=123",
        "桜都字幕组", std::filesystem::temp_directory_path().generic_string()};
    const auto rule = nlohmann::json::parse(QbRssRules::mikanRuleJson(spec));
    REQUIRE(rule["enabled"] == true);
    REQUIRE(rule["mustContain"] == "桜都字幕组");
    REQUIRE(rule["affectedFeeds"].size() == 1);
    REQUIRE(rule["affectedFeeds"][0] == spec.feedUrl);
    REQUIRE(rule["savePath"] == spec.savePath);
    REQUIRE(rule["smartFilter"] == false);
}

TEST_CASE("automatic Mikan rule accepts the first recognized resource per episode") {
    const anime_vault::QbMikanRuleSpec spec{
        "AnimeVault Auto 4011", "https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203",
        "", std::filesystem::temp_directory_path().generic_string()};
    const auto rule = nlohmann::json::parse(QbRssRules::autoMikanRuleJson(spec));
    REQUIRE(rule["enabled"] == true);
    REQUIRE_FALSE(rule["mustContain"].get<std::string>().empty());
    REQUIRE(rule["useRegex"] == true);
    REQUIRE(rule["mustNotContain"] == "REPACK");
    REQUIRE(rule["addPaused"] == false);
    REQUIRE(rule["ignoreDays"] == 0);
    REQUIRE(rule["smartFilter"] == true);
    REQUIRE(rule["affectedFeeds"] == nlohmann::json::array({spec.feedUrl}));
    REQUIRE(rule["savePath"] == spec.savePath);
    const auto pending = nlohmann::json::parse(QbRssRules::autoMikanRuleJson(spec, false));
    REQUIRE(pending["enabled"] == false);
    REQUIRE(pending["affectedFeeds"] == rule["affectedFeeds"]);
}

TEST_CASE("automatic rule waits for a fetched feed before marking old articles read") {
    const std::string url = "https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203";
    REQUIRE_FALSE(QbRssRules::readyMikanFeedPath(R"({"loading":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203","isLoading":true,"hasError":false,"articles":[]}})", url));
    REQUIRE(QbRssRules::readyMikanFeedPath(R"({"Mikan Project - Anime":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=4011&subgroupid=203","isLoading":false,"hasError":false,"articles":[{"title":"Anime [01]"}]}})", url) == "Mikan Project - Anime");
}

TEST_CASE("automatic episode preferences replace unsafe defaults with five episode-only filters") {
    const auto settings = nlohmann::json::parse(QbRssRules::autoEpisodePreferencesJson(
        R"({"rss_smart_episode_filters":"old unsafe filter"})"));
    REQUIRE(settings["rss_download_repack_proper_episodes"] == false);
    const auto filters = settings["rss_smart_episode_filters"].get<std::string>();
    REQUIRE(std::count(filters.begin(), filters.end(), '\n') == 4);
    REQUIRE(filters.find("old unsafe filter") == std::string::npos);
    REQUIRE(filters.find(R"(\bs\d{1,2}e0*(\d{1,3})(?:v\d+)?\b)") != std::string::npos);
    REQUIRE(nlohmann::json::parse(QbRssRules::autoEpisodePreferencesJson(settings.dump())) == settings);
}

TEST_CASE("qB preserves multiple Mikan aliases and rejects normalized alias conflicts") {
    const auto catalog = QbRssRules::parseMikanItems(R"({"feed":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=1","articles":[
        {"title":"[G] 安妮的童话 / 安妮·雪莉 / Anne Shirley - 17 [1080P]"},
        {"title":"[G] 中文甲 / Shared Name [01][1080P]"},
        {"title":"[G] 中文乙 / Shared_Name [02][1080P]"}
    ]}})");
    REQUIRE(catalog.pairs.size() == 2);
    for (const auto& pair : catalog.pairs) CHECK(pair.canonicalTitle == "安妮的童话");
}

TEST_CASE("single anime Mikan feeds resolve the four local unmatched releases") {
    const auto catalog = QbRssRules::parseMikanItems(R"({
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
    const anime_vault::QbMikanRuleSpec spec{"auto", "https://mikanani.me/RSS/Bangumi?bangumiId=1", "", std::filesystem::temp_directory_path().generic_string()};
    const auto history = nlohmann::json::parse(QbRssRules::autoMikanHistoryJson(spec,
        R"json({"old":{"affectedFeeds":["https://mikanani.me/RSS/Bangumi?bangumiId=1"],"previouslyMatchedEpisodes":["001","1080","2026","1x2"]},"other":{"affectedFeeds":["other"],"previouslyMatchedEpisodes":["9"]}})json",
        R"json({"feed":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=1","articles":[{"title":"Anime [02v2]","isRead":false},{"title":"Anime - 03 (1080p)","isRead":true},{"title":"Anime [04]","isRead":true}]}})json",
        R"json([{"name":"Anime [02v2]"},{"name":"Anime - 03 (1080p)"},{"name":"Other [09]"}])json"));
    REQUIRE(history == nlohmann::json::array({"1", "2", "3"}));
}

TEST_CASE("qB domain values preserve empty defaults without adapter dependencies") {
    const anime_vault::QbStatus status;
    CHECK_FALSE(status.configured);
    CHECK_FALSE(status.connected);
    CHECK(status.errorCode.empty());
    CHECK(status.version.empty());
    CHECK(status.torrentCount == 0);
    CHECK(status.completedCount == 0);
    CHECK(status.completedPaths.empty());
    CHECK_FALSE(anime_vault::QbActionResult{}.success);
    CHECK(anime_vault::QbEndpoint{}.port == 0);
}

TEST_CASE("pure Mikan catalog keeps response article and traversal bounds") {
    CHECK_THROWS_AS(QbRssRules::parseMikanItems(""), std::invalid_argument);
    CHECK_THROWS_AS(QbRssRules::parseMikanItems(std::string(4 * 1024 * 1024 + 1, ' ')), std::invalid_argument);
    CHECK_THROWS_AS(QbRssRules::parseMikanItems("[]"), std::invalid_argument);
    nlohmann::json feed = {{"url", "https://mikanani.me/RSS/Bangumi?bangumiId=1"},
                           {"articles", std::vector<nlohmann::json>(5000)}};
    CHECK(QbRssRules::parseMikanItems(nlohmann::json{{"feed", feed}}.dump()).articleCount == 5000);
    feed["articles"].push_back(nullptr);
    CHECK_THROWS_AS(QbRssRules::parseMikanItems(nlohmann::json{{"feed", feed}}.dump()), std::invalid_argument);
    feed["articles"] = nlohmann::json::array();
    for (int depth = 0; depth < 8; ++depth) feed = nlohmann::json{{"folder", feed}};
    CHECK(QbRssRules::parseMikanItems(feed.dump()).feedCount == 1);
    CHECK(QbRssRules::parseMikanItems(nlohmann::json{{"tooDeep", feed}}.dump()).feedCount == 0);
}

TEST_CASE("pure Mikan URL whitelist preserves query characters and bounds") {
    for (const auto* route : {"Bangumi", "MyBangumi", "Search"})
        CHECK(QbRssRules::validMikanFeedUrl(std::string("https://www.mikanani.kas.pub/RSS/") + route + "?a=._~%=&+-0123"));
    CHECK(QbRssRules::validMikanFeedUrl("HTTPS://MIKANANI.ME/rss/bangumi"));
    CHECK(QbRssRules::validMikanFeedUrl("https://mikanani.me/RSS/Search?" + std::string(768, 'a')));
    CHECK_FALSE(QbRssRules::validMikanFeedUrl("https://mikanani.me/RSS/Search?" + std::string(769, 'a')));
    for (const auto* suffix : {"?", "?x=a b", "?x=a/b", "?x=a#b", "/extra", "?x=a\nb"})
        CHECK_FALSE(QbRssRules::validMikanFeedUrl(std::string("https://mikanani.me/RSS/Bangumi") + suffix));
}

TEST_CASE("pure keyword rules retain validation distinct from automatic episode rules") {
    const anime_vault::QbMikanRuleSpec valid{"rule", "https://mikanani.me/RSS/Bangumi?bangumiId=1",
                                         "keyword", std::filesystem::temp_directory_path().generic_string()};
    const auto rejects = [&](const auto& change) {
        auto invalid = valid;
        change(invalid);
        CHECK_THROWS_AS(QbRssRules::mikanRuleJson(invalid), std::invalid_argument);
    };
    rejects([](auto& spec) { spec.ruleName.clear(); });
    rejects([](auto& spec) { spec.ruleName = std::string(81, 'x'); });
    rejects([](auto& spec) { spec.ruleName += '\n'; });
    rejects([](auto& spec) { spec.keyword.clear(); });
    rejects([](auto& spec) { spec.keyword = std::string(121, 'x'); });
    rejects([](auto& spec) { spec.keyword += '\x7f'; });
    rejects([](auto& spec) { spec.feedUrl = "https://example.com/rss"; });
    rejects([](auto& spec) { spec.savePath.clear(); });
    rejects([](auto& spec) { spec.savePath = "relative"; });
    rejects([](auto& spec) { spec.savePath += "/../escape"; });
    rejects([](auto& spec) { spec.savePath += std::string(1025, 'x'); });
    auto automatic = valid;
    automatic.keyword.clear();
    CHECK(nlohmann::json::parse(QbRssRules::autoMikanRuleJson(automatic))["smartFilter"] == true);
    CHECK(nlohmann::json::parse(QbRssRules::mikanRuleJson(valid))["useRegex"] == false);
}

TEST_CASE("pure episode patterns and preferences retain exact regex semantics") {
    CHECK(QbRssRules::bracketEpisodePattern() == R"((?<=\[)0*(\d{1,3})(?:v\d+)?(?=\]))");
    CHECK(QbRssRules::dashedEpisodePattern() == R"((?<=-\x20)0*(\d{1,3})(?:v\d+)?(?=\s*(?:\[|\(|-)))");
    for (const auto* invalid : {"[]", "{}", R"({"rss_smart_episode_filters":false})"})
        CHECK_THROWS_AS(QbRssRules::autoEpisodePreferencesJson(invalid), std::invalid_argument);
}

TEST_CASE("pure feed readiness requires a nonempty successful matching top-level feed") {
    const std::string url = "https://mikanani.me/RSS/Bangumi?bangumiId=1";
    CHECK_THROWS_AS(QbRssRules::readyMikanFeedPath("[]", url), std::invalid_argument);
    nlohmann::json feed = {{"url", url}, {"isLoading", false}, {"hasError", false},
                           {"articles", nlohmann::json::array({{{"title", "Anime [01]"}}})}};
    CHECK(QbRssRules::readyMikanFeedPath(nlohmann::json{{"ready", feed}}.dump(), url) == "ready");
    CHECK_FALSE(QbRssRules::readyMikanFeedPath(nlohmann::json{{"folder", {{"nested", feed}}}}.dump(), url));
    feed["hasError"] = true;
    CHECK_FALSE(QbRssRules::readyMikanFeedPath(nlohmann::json{{"error", feed}}.dump(), url));
    feed["hasError"] = false;
    feed.erase("isLoading");
    CHECK_FALSE(QbRssRules::readyMikanFeedPath(nlohmann::json{{"unknown", feed}}.dump(), url));
}

TEST_CASE("pure history keeps case-normalized task hashes and rejects unrelated rule histories") {
    const anime_vault::QbMikanRuleSpec spec{"auto", "https://mikanani.me/RSS/Bangumi?bangumiId=1", "", ""};
    const auto history = nlohmann::json::parse(QbRssRules::autoMikanHistoryJson(spec,
        R"({"mixed":{"affectedFeeds":["https://mikanani.me/RSS/Bangumi?bangumiId=1","other"],"previouslyMatchedEpisodes":["9"]}})",
        R"({"folder":{"feed":{"url":"https://mikanani.me/RSS/Bangumi?bangumiId=1","articles":[{"title":"Anime [02]","torrentURL":"magnet:?xt=urn:btih:ABCDEF0123456789ABCDEF0123456789ABCDEF01"},{"title":"Anime [03]"},{"title":"Anime [04]","isRead":true}]}}})",
        R"([{"hash":"abcdef0123456789abcdef0123456789abcdef01"},{"name":"Anime [03].mkv"}])"));
    CHECK(history == nlohmann::json::array({"2", "3"}));
    CHECK_THROWS_AS(QbRssRules::autoMikanHistoryJson(spec, "[]", "{}", "[]"), std::invalid_argument);
    CHECK_THROWS_AS(QbRssRules::autoMikanHistoryJson(spec, "{}", "[]", "[]"), std::invalid_argument);
    CHECK_THROWS_AS(QbRssRules::autoMikanHistoryJson(spec, "{}", "{}", "{}"), std::invalid_argument);
}
