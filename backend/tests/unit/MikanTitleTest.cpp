#include "anime_vault/services/MikanTitleParser.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>

using anime_vault::MikanTitleParser;

TEST_CASE("Mikan handles spaced revision tags and reversed bilingual titles") {
    const auto pair = MikanTitleParser::parse("【字幕组】 Roman Title／中文标题 [ 01v2 ] [1080P]");
    REQUIRE(pair);
    CHECK(pair->canonicalTitle == "中文标题");
    CHECK(pair->alias == "Roman Title");
    CHECK(pair->episode == 1);
    const auto dashed = MikanTitleParser::parse("[ANi] A Tale of the Secret Saint / 转生后的大圣女，极力隐瞒圣女的身份 - 03 [1080P][Baha]");
    REQUIRE(dashed);
    CHECK(dashed->canonicalTitle == "转生后的大圣女，极力隐瞒圣女的身份");
    CHECK(dashed->alias == "A Tale of the Secret Saint");
    REQUIRE_FALSE(MikanTitleParser::parse("[G] Ranma1/2 [03][1080P]"));
    REQUIRE_FALSE(MikanTitleParser::parse("[G] Fate/Zero [03][1080P]"));
    const auto slashName = MikanTitleParser::parseAll("[G] 中文标题 / Fate/Zero [03][1080P]");
    REQUIRE(slashName.size() == 1);
    CHECK(slashName[0].alias == "Fate/Zero");
    REQUIRE_FALSE(MikanTitleParser::parse("[G] 中文 / Roman - 01-02 [1080P]"));
    REQUIRE_FALSE(MikanTitleParser::parse("[G] 中文 / Roman - 01+02 [1080P]"));
}

TEST_CASE("Mikan article separates canonical Chinese title and romanized alias") {
    const auto parsed = MikanTitleParser::parse(
        "[桜都字幕组] 与奔驰于透明之夜的你，谈一场看不见的恋爱。 / "
        "Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita. "
        "[03][1080P][简体内嵌]");
    REQUIRE(parsed);
    REQUIRE(parsed->canonicalTitle == "与奔驰于透明之夜的你，谈一场看不见的恋爱。");
    REQUIRE(parsed->alias == "Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita");
    REQUIRE(parsed->episode == 3);
}

TEST_CASE("Mikan parser rejects articles without an explicit bilingual episode mapping") {
    REQUIRE_FALSE(MikanTitleParser::parse("[ANi] A Single Title [03][1080P]"));
    REQUIRE_FALSE(MikanTitleParser::parse("[ANi] One / Two [1080P]"));
    REQUIRE_FALSE(MikanTitleParser::parse("[ANi] One / Two [00][1080P]"));
}

TEST_CASE("Mikan article accepts three language names and a dashed episode") {
    const auto parsed = MikanTitleParser::parse(
        "[三明治摆烂组&Y-Raws] 才女的侍从 在满是高岭之花的贵族学校暗中照顾（毫无生活自理能力的）学院第一大小姐"
        " / Saijo no Osewa / 才女のお世话 - 08 - [简繁日内封][HEVC-10bit 1080P]");
    REQUIRE(parsed);
    REQUIRE(parsed->canonicalTitle ==
        "才女的侍从 在满是高岭之花的贵族学校暗中照顾（毫无生活自理能力的）学院第一大小姐");
    REQUIRE(parsed->alias == "Saijo no Osewa");
    REQUIRE(parsed->episode == 8);
    REQUIRE_FALSE(MikanTitleParser::parse(
        "[三明治摆烂组] 中文 / Roman / 日文 - 00 - [简日内嵌][1080P]"));
}

TEST_CASE("Mikan fixed random RSS sample preserves every unambiguous language alias") {
    std::ifstream input(MIKAN_FIXTURE_PATH);
    REQUIRE(input);
    const auto fixture = nlohmann::json::parse(input);
    REQUIRE(fixture["samples"].size() == 18);
    for (const auto& sample : fixture["samples"]) {
        const auto title = sample["title"].get<std::string>();
        CAPTURE(title, sample["url"]);
        const auto pairs = MikanTitleParser::parseAll(title);
        if (sample["expected"].is_null()) { CHECK(pairs.empty()); continue; }
        const auto& expected = sample["expected"];
        REQUIRE(pairs.size() == expected["aliases"].size());
        for (std::size_t i = 0; i < pairs.size(); ++i) {
            CHECK(pairs[i].canonicalTitle == expected["canonical"].get<std::string>());
            CHECK(pairs[i].alias == expected["aliases"][i].get<std::string>());
            CHECK(pairs[i].episode == expected["episode"].get<int>());
        }
    }
}

TEST_CASE("Mikan preserves bracketed bilingual release title as a filename alias") {
    const auto pairs = MikanTitleParser::parseAll("【悠哈璃羽字幕社】[暗芝居 第十七季_Yami Shibai 17][12][1080P]");
    REQUIRE(pairs.size() == 2);
    CHECK(pairs[0].alias == "Yami Shibai 17");
    CHECK(pairs[1].alias == "暗芝居 第十七季_Yami Shibai 17");
}
