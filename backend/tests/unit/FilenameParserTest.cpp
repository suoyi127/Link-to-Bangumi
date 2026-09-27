#include "FilenameFixture.hpp"
#include "anime_vault/services/FilenameParser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

using anime_vault::EpisodeNumber;
using anime_vault::EpisodeType;
using anime_vault::FilenameParser;
using anime_vault::test_support::ConfidenceBand;
using anime_vault::test_support::confidenceBand;
using anime_vault::test_support::loadFilenameFixtures;

TEST_CASE("filename parser matches every approved fixture expectation") {
    const auto fixtures = loadFilenameFixtures(FIXTURE_PATH);
    REQUIRE(fixtures.size() == 57);
    const FilenameParser parser;
    for (const auto& expected : fixtures) {
        CAPTURE(expected.filename);
        const auto actual = parser.parse(expected.filename);
        CHECK(actual.originalName == expected.filename);
        CHECK(actual.title == expected.title);
        CHECK(actual.season == expected.season);
        CHECK(actual.episode == expected.episode);
        CHECK(actual.episodeType == expected.episodeType);
        CHECK(actual.releaseGroup == expected.releaseGroup);
        CHECK(actual.resolution == expected.resolution);
        CHECK(actual.version == expected.version);
        CHECK(confidenceBand(actual.confidence) == expected.confidenceBand);
        CHECK(actual.warnings == expected.warnings);
    }
}

TEST_CASE("numbers in titles are retained when a separate episode is present") {
    const FilenameParser parser;
    const auto eightySix = parser.parse("86 - 03 [1080p].mkv");
    REQUIRE(eightySix.title == "86");
    REQUIRE(eightySix.episode == EpisodeNumber::parse("3"));

    const auto dimension = parser.parse("2.5次元の誘惑 - 04 [1080p].mkv");
    REQUIRE(dimension.title == "2.5次元の誘惑");
    REQUIRE(dimension.episode == EpisodeNumber::parse("4"));
}

TEST_CASE("release filename title drops only its trailing separator") {
    const FilenameParser parser;
    const auto result = parser.parse(
        "[Sakurato] Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita. "
        "[01][AVC-8bit 1080P AAC][CHS].mkv");

    REQUIRE(result.releaseGroup == "Sakurato");
    REQUIRE(result.title == "Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita");
    REQUIRE(result.episode == EpisodeNumber::parse("1"));
    REQUIRE(result.resolution == "1080p");
}

TEST_CASE("dash and decimal episode tokens take priority over special tokens") {
    const FilenameParser parser;
    const auto dash = parser.parse("Moonbound - SP01 - 02.mkv");
    REQUIRE(dash.episode == EpisodeNumber::parse("2"));
    REQUIRE(dash.episodeType == EpisodeType::normal);

    const auto decimal = parser.parse("Moonbound - OVA01 - 12.5.mkv");
    REQUIRE(decimal.episode == EpisodeNumber::parse("12.5"));
    REQUIRE(decimal.episodeType == EpisodeType::normal);

    const auto specialOnly = parser.parse("Moonbound - NCOP01.mkv");
    REQUIRE(specialOnly.episode == EpisodeNumber::parse("1"));
    REQUIRE(specialOnly.episodeType == EpisodeType::ncop);
}

TEST_CASE("special episode types are recognized regardless of letter case") {
    const FilenameParser parser;
    for (const auto& [filename, expectedType] : {
             std::pair<std::string_view, EpisodeType>{"Moonbound - Ova01.mkv", EpisodeType::ova},
             {"Moonbound - Ncop02.mkv", EpisodeType::ncop},
             {"Moonbound - Nced03.mkv", EpisodeType::nced}}) {
        CAPTURE(filename);
        const auto result = parser.parse(filename);
        CHECK(result.episodeType == expectedType);
    }
}

TEST_CASE("batch words in a title do not override an explicit episode") {
    const FilenameParser parser;
    const auto result = parser.parse("The Bad Batch - 01.mkv");
    REQUIRE(result.title == "The Bad Batch");
    REQUIRE(result.episode == EpisodeNumber::parse("1"));
    REQUIRE(result.episodeType == EpisodeType::normal);
    REQUIRE(confidenceBand(result.confidence) == ConfidenceBand::high);
    REQUIRE(result.warnings.empty());
}

TEST_CASE("case-insensitive batch metadata is removed from the title") {
    const FilenameParser parser;
    const auto result = parser.parse("Moonbound complete batch [1080p].mkv");
    REQUIRE(result.title == "Moonbound");
    REQUIRE_FALSE(result.episode.has_value());
    REQUIRE(result.resolution == "1080p");
    REQUIRE(confidenceBand(result.confidence) == ConfidenceBand::low);
    REQUIRE(result.warnings == std::vector<std::string>{"batch_release"});
}

TEST_CASE("technical brackets before a title do not erase it") {
    const FilenameParser parser;
    const auto resolution = parser.parse("[1080p] Moonbound - 01.mkv");
    REQUIRE(resolution.title == "Moonbound");
    REQUIRE(resolution.episode == EpisodeNumber::parse("1"));
    REQUIRE(resolution.resolution == "1080p");
    REQUIRE_FALSE(resolution.releaseGroup.has_value());

    const auto language = parser.parse("[Group] [CHS] Moonbound - 01.mkv");
    REQUIRE(language.title == "Moonbound");
    REQUIRE(language.releaseGroup == "Group");
    REQUIRE(language.episode == EpisodeNumber::parse("1"));
}

TEST_CASE("missing episode never receives high confidence") {
    const FilenameParser parser;
    const auto result = parser.parse("Moonbound [1080p].mkv");
    REQUIRE_FALSE(result.episode.has_value());
    REQUIRE(confidenceBand(result.confidence) == ConfidenceBand::low);
    REQUIRE(result.warnings == std::vector<std::string>{"episode_missing"});
}

TEST_CASE("parser input is a filename, not a path") {
    const FilenameParser parser;
    for (std::string_view path : {"C:/Shows/Moonbound - 01.mkv", "Shows\\Moonbound - 01.mkv"}) {
        const auto result = parser.parse(path);
        CAPTURE(path);
        CHECK_FALSE(result.episode.has_value());
        CHECK(confidenceBand(result.confidence) == ConfidenceBand::low);
        CHECK_FALSE(result.warnings.empty());
    }
}

TEST_CASE("empty and malformed filenames return safe low-confidence results") {
    const FilenameParser parser;
    for (std::string_view name : {"", "   ", "[] .mkv", "...",
                                  "Moonbound [03v9999999999999999999999999999999999].mkv"}) {
        CAPTURE(name);
        const auto result = parser.parse(name);
        CHECK(result.originalName == name);
        CHECK_FALSE(result.episode.has_value());
        CHECK(confidenceBand(result.confidence) == ConfidenceBand::low);
        CHECK_FALSE(result.warnings.empty());
    }
}
