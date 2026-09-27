#include "anime_vault/domain/EpisodeNumber.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

using anime_vault::EpisodeNumber;

TEST_CASE("EpisodeNumber parses integer and one-digit decimal values exactly") {
    for (const auto [text, expected] : {
             std::pair{"12", "12"}, std::pair{"12.5", "12.5"},
             std::pair{"0.0", "0.0"}, std::pair{"4294967295", "4294967295"}}) {
        const auto parsed = EpisodeNumber::parse(text);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->toString() == expected);
    }
}

TEST_CASE("EpisodeNumber rejects noncanonical decimal syntax and invalid text") {
    for (const std::string_view text : {
             "", "12.50", "-1", "+1", " 12", "12 ", "1 2", "12.",
             ".5", "1.2.3", "12a", "4294967296", "4294967295.10"}) {
        CAPTURE(text);
        REQUIRE_FALSE(EpisodeNumber::parse(text).has_value());
    }
}

TEST_CASE("EpisodeNumber fromParts preserves both components") {
    const auto integer = EpisodeNumber::fromParts(12, std::nullopt);
    REQUIRE(integer.whole() == 12);
    REQUIRE_FALSE(integer.tenth().has_value());
    REQUIRE(integer.toString() == "12");

    const auto decimal = EpisodeNumber::fromParts(12, 5);
    REQUIRE(decimal.whole() == 12);
    REQUIRE(decimal.tenth() == 5u);
    REQUIRE(decimal.toString() == "12.5");
}

TEST_CASE("EpisodeNumber fromParts rejects more than one decimal digit") {
    REQUIRE_THROWS_AS(EpisodeNumber::fromParts(12, 10), std::invalid_argument);
    REQUIRE_THROWS_AS(EpisodeNumber::fromParts(12, std::numeric_limits<std::uint32_t>::max()),
                      std::invalid_argument);
}

TEST_CASE("EpisodeNumber compares by whole number then exact tenth representation") {
    const auto twelve = EpisodeNumber::fromParts(12, std::nullopt);
    const auto twelvePointZero = EpisodeNumber::fromParts(12, 0);
    const auto twelvePointFive = EpisodeNumber::fromParts(12, 5);
    const auto thirteen = EpisodeNumber::fromParts(13, std::nullopt);

    REQUIRE(twelve == EpisodeNumber::fromParts(12, std::nullopt));
    REQUIRE(twelve < twelvePointZero);
    REQUIRE(twelvePointZero < twelvePointFive);
    REQUIRE(twelvePointFive < thirteen);
}
