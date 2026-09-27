#include "anime_vault/services/MikanTitleParser.hpp"

#include <catch2/catch_test_macros.hpp>

using anime_vault::MikanTitleParser;

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
