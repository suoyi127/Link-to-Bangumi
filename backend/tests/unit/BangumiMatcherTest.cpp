#include "anime_vault/services/BangumiMatcher.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace anime_vault;

TEST_CASE("Bangumi matcher ranks animation titles conservatively") {
    const BangumiMatchQuery query{"葬送的芙莉莲", 2023, 28};
    const std::vector<BangumiSubject> subjects{
        {30, "Unrelated Show", "", "2023-09-29", "", 28, 2},
        {12, "Sousou no Frieren", "葬送的芙莉莲", "2023-09-29", "", 28, 2},
        {11, "葬送的芙莉莲", "", "2023-09-29", "", 28, 2},
        {13, "Sousou no Frieren", "葬送的芙莉莲", "2023-09-29", "", 28, 2},
        {40, "葬送的芙莉莲", "", "2023-09-29", "", 28, 1},
        {50, "葬送的芙莉莲：旅途", "", "2023-09-29", "", 28, 2},
        {51, "葬送的芙莉莲 Part 2", "", "2023-09-29", "", 28, 2},
        {52, "葬送的芙莉莲 Special", "", "2023-09-29", "", 28, 2},
    };
    const auto ranked = rankBangumiCandidates(query, subjects);
    REQUIRE(ranked.items.size() == 5);
    CHECK(ranked.items[0].id == 11);
    CHECK(ranked.items[1].id == 12);
    CHECK(ranked.items[2].id == 13);
    CHECK(ranked.items[0].score > ranked.items[1].score);
    CHECK(ranked.items[1].score == ranked.items[2].score);
    CHECK_FALSE(ranked.autoBindEligible);
    for (const auto& candidate : ranked.items) {
        CHECK(candidate.type == 2);
        CHECK(candidate.id != 30);
        CHECK(candidate.score >= 0.0);
        CHECK(candidate.score <= 1.0);
    }

    const auto tie = rankBangumiCandidates({"葬送的芙莉莲", {}, {}}, {
        {2, "葬送的芙莉莲", "", "", "", 0, 2},
        {1, "葬送的芙莉莲", "", "", "", 0, 2},
    });
    REQUIRE(tie.items.size() == 2);
    CHECK(tie.items[0].id == 1);
    CHECK(tie.items[1].id == 2);
    CHECK_FALSE(tie.autoBindEligible);

    const auto unique = rankBangumiCandidates({" 葬送的芙莉莲！ ", {}, {}}, {
        {1, "葬送的芙莉莲", "", "", "", 0, 2},
        {2, "Different series", "", "", "", 0, 2},
    });
    REQUIRE(unique.items.size() == 1);
    CHECK(unique.items[0].id == 1);
    CHECK(unique.autoBindEligible);

    const auto unrelated = rankBangumiCandidates(query, {subjects.front()});
    CHECK(unrelated.items.empty());
    CHECK_FALSE(unrelated.autoBindEligible);

    const auto ascii = rankBangumiCandidates({"  MY-HERO Academia! ", {}, {}}, {
        {8, "My Hero Academia", "", "", "", 0, 2},
    });
    REQUIRE(ascii.items.size() == 1);
    CHECK(ascii.items[0].score >= 0.92);
    CHECK(ascii.autoBindEligible);

    const auto japanese = rankBangumiCandidates({"「葬送のフリーレン」", {}, {}}, {
        {9, "葬送のフリーレン", "", "", "", 0, 2},
    });
    REQUIRE(japanese.items.size() == 1);
    CHECK(japanese.items[0].score >= 0.92);

    const auto fullwidthPeriod = rankBangumiCandidates({"葬送的芙莉莲．", {}, {}}, {
        {10, "葬送的芙莉莲", "", "", "", 0, 2},
    });
    REQUIRE(fullwidthPeriod.items.size() == 1);
    CHECK(fullwidthPeriod.items[0].score >= 0.92);
    CHECK(fullwidthPeriod.autoBindEligible);

    for (const std::string& title : {"葬送の〜フリーレン", "葬送の～フリーレン"}) {
        const auto waveDash = rankBangumiCandidates({title, {}, {}}, {
            {11, "葬送のフリーレン", "", "", "", 0, 2},
        });
        REQUIRE(waveDash.items.size() == 1);
        CHECK(waveDash.items[0].score >= 0.92);
        CHECK(waveDash.autoBindEligible);
    }

    const auto iterationMark = rankBangumiCandidates({"時々", {}, {}}, {
        {12, "時", "", "", "", 0, 2},
    });
    CHECK_FALSE(iterationMark.autoBindEligible);
    if (!iterationMark.items.empty()) CHECK(iterationMark.items[0].score < 0.92);

    const auto prolongedSound = rankBangumiCandidates({"フリーレン", {}, {}}, {
        {13, "フリレン", "", "", "", 0, 2},
    });
    CHECK_FALSE(prolongedSound.autoBindEligible);
    if (!prolongedSound.items.empty()) CHECK(prolongedSound.items[0].score < 0.92);

    for (const BangumiMatchQuery mismatchQuery : {
             BangumiMatchQuery{"葬送的芙莉莲", 2024, {}},
             BangumiMatchQuery{"葬送的芙莉莲", {}, 12},
             BangumiMatchQuery{"葬送的芙莉莲", 2024, 12},
         }) {
        const auto mismatch = rankBangumiCandidates(mismatchQuery, {
            {14, "葬送的芙莉莲", "", "2023-09-29", "", 28, 2},
        });
        REQUIRE(mismatch.items.size() == 1);
        CHECK(mismatch.items[0].score < 0.92);
        CHECK_FALSE(mismatch.autoBindEligible);
    }

    const std::string longPrefix(256, 'a');
    const auto longTitle = rankBangumiCandidates({longPrefix + "x", {}, {}}, {
        {15, longPrefix + "y", "", "", "", 0, 2},
    });
    CHECK_FALSE(longTitle.autoBindEligible);
    if (!longTitle.items.empty()) CHECK(longTitle.items[0].score < 0.92);

    const auto thresholdMargin = rankBangumiCandidates({"Title", 2023, 12}, {
        {16, "Title", "", "2023-01-01", "", 12, 2},
        {17, "Other", "Title", "", "", 0, 2},
    });
    REQUIRE(thresholdMargin.items.size() == 2);
    CHECK(thresholdMargin.items[0].score == 1.0);
    CHECK(thresholdMargin.items[1].score == 0.92);
    CHECK(thresholdMargin.autoBindEligible);
}
