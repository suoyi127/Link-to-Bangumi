#include "FilenameFixture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using anime_vault::test_support::ConfidenceBand;
using anime_vault::test_support::FilenameFixture;
using anime_vault::test_support::confidenceBand;
using anime_vault::test_support::loadFilenameFixtures;

namespace {

class TemporaryFixtureFile {
public:
    explicit TemporaryFixtureFile(std::string_view content) {
        static std::atomic<unsigned> sequence{0};
        bool created = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            directory_ = std::filesystem::temp_directory_path() /
                         ("anime-vault-fixture-" +
                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                          "-" + std::to_string(sequence++));
            if (std::filesystem::create_directory(directory_)) {
                created = true;
                break;
            }
        }
        if (!created) {
            throw std::runtime_error("could not create temporary fixture directory");
        }
        path_ = directory_ / "case.json";
        std::ofstream output(path_, std::ios::binary);
        if (!output || !(output << content)) {
            output.close();
            cleanup();
            throw std::runtime_error("could not create temporary fixture file");
        }
    }

    ~TemporaryFixtureFile() { cleanup(); }

    TemporaryFixtureFile(const TemporaryFixtureFile&) = delete;
    TemporaryFixtureFile& operator=(const TemporaryFixtureFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    void cleanup() noexcept {
        std::error_code error;
        std::filesystem::remove(path_, error);
        error.clear();
        std::filesystem::remove(directory_, error);
    }

    std::filesystem::path directory_;
    std::filesystem::path path_;
};

std::string replaceOnce(std::string text, std::string_view oldText, std::string_view newText) {
    const auto position = text.find(oldText);
    if (position == std::string::npos) {
        throw std::logic_error("test fixture replacement was not found");
    }
    text.replace(position, oldText.size(), newText);
    return text;
}

}  // namespace

TEST_CASE("filename fixture corpus has the required schema and size") {
    std::ifstream input(FIXTURE_PATH);
    REQUIRE(input.is_open());

    const auto cases = nlohmann::json::parse(input);
    REQUIRE(cases.is_array());
    REQUIRE(cases.size() >= 50);

    for (const auto& item : cases) {
        REQUIRE(item.is_object());
        REQUIRE(item.contains("filename"));
        REQUIRE(item["filename"].is_string());
        REQUIRE(item.contains("title"));
        REQUIRE(item["title"].is_string());
        REQUIRE(item.contains("episode_type"));
        REQUIRE(item["episode_type"].is_string());
        REQUIRE(item.contains("confidence_band"));
        REQUIRE(item["confidence_band"].is_string());
        REQUIRE(item.contains("warnings"));
        REQUIRE(item["warnings"].is_array());
        for (const auto& warning : item["warnings"]) {
            REQUIRE(warning.is_string());
        }
        for (const auto* field : {"season", "version"}) {
            if (item.contains(field)) REQUIRE(item[field].is_number_unsigned());
        }
        for (const auto* field : {"episode", "release_group", "resolution"}) {
            if (item.contains(field)) REQUIRE(item[field].is_string());
        }
    }
}

TEST_CASE("filename fixture loader preserves distinct expectations and required coverage") {
    const auto cases = loadFilenameFixtures(FIXTURE_PATH);
    REQUIRE(cases.size() >= 50);

    std::set<std::string> names;
    for (const auto& item : cases) {
        CAPTURE(item.filename);
        REQUIRE(names.insert(item.filename).second);
        REQUIRE_FALSE(item.title.empty());
    }

    const auto hasName = [&](std::string_view name) {
        return std::ranges::any_of(cases, [name](const FilenameFixture& item) {
            return item.filename == name;
        });
    };
    const auto hasPart = [&](std::string_view part) {
        return std::ranges::any_of(cases, [part](const FilenameFixture& item) {
            return item.filename.find(part) != std::string::npos;
        });
    };
    const auto hasTitle = [&](std::string_view title) {
        return std::ranges::any_of(cases, [title](const FilenameFixture& item) {
            return item.title == title;
        });
    };
    const auto hasType = [&](anime_vault::EpisodeType type) {
        return std::ranges::any_of(cases, [type](const FilenameFixture& item) {
            return item.episodeType == type;
        });
    };

    REQUIRE(hasName("[LoliHouse] Yuusha no Rokkotsu de - 09 [WebRip 1080p HEVC-10bit AAC SRTx2].mkv"));
    REQUIRE(hasName("[Sakurato] Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita. [01][AVC-8bit 1080P AAC][CHS].mp4"));
    REQUIRE(hasName("[Sakurato] Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita. [01][HEVC-10bit 1080P AAC][CHS&CHT].mkv"));
    REQUIRE(hasTitle("葬送的芙莉莲"));
    REQUIRE(hasTitle("ぼっち・ざ・ろっく！"));
    REQUIRE(hasTitle("The Blue Atlas"));
    REQUIRE(hasTitle("魔法少女 Aurora"));
    REQUIRE(hasPart("S02E03"));
    REQUIRE(hasPart(" E04"));
    REQUIRE(hasPart(" EP05"));
    REQUIRE(hasPart("[06]"));
    REQUIRE(hasPart("[07v2]"));
    REQUIRE(hasPart(" - 08"));
    REQUIRE(hasPart("12.5"));
    REQUIRE(hasType(anime_vault::EpisodeType::sp));
    REQUIRE(hasType(anime_vault::EpisodeType::ova));
    REQUIRE(hasType(anime_vault::EpisodeType::ncop));
    REQUIRE(hasType(anime_vault::EpisodeType::nced));
    REQUIRE(hasPart("01-12"));
    REQUIRE(hasPart("01+02"));
    REQUIRE(hasPart("Batch"));
    REQUIRE(hasName("Moonbound [1080p].mkv"));
    REQUIRE(hasTitle("86"));
    REQUIRE(hasTitle("2.5次元の誘惑"));
    REQUIRE(hasName("Moonbound 09.mkv"));
    REQUIRE(hasName("银河列车 12.mkv"));
    REQUIRE(hasName("雨のアトリエ 04.mkv"));
    for (const auto band : {ConfidenceBand::high, ConfidenceBand::medium, ConfidenceBand::low}) {
        REQUIRE(std::ranges::any_of(cases, [band](const FilenameFixture& item) {
            return item.confidenceBand == band;
        }));
    }
    for (const auto& item : cases) {
        if (item.confidenceBand == ConfidenceBand::medium) {
            REQUIRE(item.episode.has_value());
            REQUIRE(item.warnings == std::vector<std::string>{"ambiguous_episode"});
        }
    }
}

TEST_CASE("filename fixture loader rejects malformed and ambiguous expectations") {
    const std::string valid = R"({"filename":"A.mkv","title":"A","episode":"1","episode_type":"normal","confidence_band":"high","warnings":[]})";
    const auto arrayOf = [](const std::string& item) { return "[" + item + "]"; };
    struct InvalidCase {
        std::string_view label;
        std::string json;
        std::string_view expectedDiagnostic;
    };
    const std::vector<InvalidCase> invalidCases{
        {"malformed JSON", "[", "invalid JSON"},
        {"wrong root type", "{}", "root must be an array"},
        {"wrong item type", "[5]", "fixture[0].item"},
        {"missing required field", arrayOf(replaceOnce(valid, R"("title":"A",)", "")),
         "fixture[0].title"},
        {"unknown field", arrayOf(replaceOnce(valid, R"("warnings":[])",
                                                R"("warnings":[],"extra":true)")),
         "fixture[0].extra"},
        {"wrong scalar type", arrayOf(replaceOnce(valid, R"("filename":"A.mkv")",
                                                    R"("filename":42)")),
         "fixture[0].filename"},
        {"filename path", arrayOf(replaceOnce(valid, R"("filename":"A.mkv")",
                                                R"("filename":"C:/Shows/A.mkv")")),
         "fixture[0].filename"},
        {"wrong array type", arrayOf(replaceOnce(valid, R"("warnings":[])",
                                                   R"("warnings":"none")")),
         "fixture[0].warnings"},
        {"wrong array element", arrayOf(replaceOnce(valid, R"("warnings":[])",
                                                      R"("warnings":[7])")),
         "fixture[0].warnings"},
        {"negative unsigned value", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                          R"("season":-1,"episode":"1")")),
         "fixture[0].season"},
        {"uint32 overflow", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                   R"("season":4294967296,"episode":"1")")),
         "fixture[0].season"},
        {"wrong release group type", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                          R"("episode":"1","release_group":false)")),
         "fixture[0].release_group"},
        {"wrong resolution type", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                       R"("episode":"1","resolution":[])")),
         "fixture[0].resolution"},
        {"wrong version type", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                    R"("episode":"1","version":1.5)")),
         "fixture[0].version"},
        {"episode wrong type", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                       R"("episode":1)")),
         "fixture[0].episode"},
        {"bad episode syntax", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                        R"("episode":"1.25")")),
         "fixture[0].episode"},
        {"noncanonical episode", arrayOf(replaceOnce(valid, R"("episode":"1")",
                                                         R"("episode":"01")")),
         "fixture[0].episode"},
        {"invalid episode type", arrayOf(replaceOnce(valid, R"("episode_type":"normal")",
                                                         R"("episode_type":"movie")")),
         "fixture[0].episode_type"},
        {"invalid confidence band", arrayOf(replaceOnce(valid, R"("confidence_band":"high")",
                                                            R"("confidence_band":"certain")")),
         "fixture[0].confidence_band"},
        {"duplicate filename", "[" + valid + "," + valid + "]", "fixture[1].filename"},
        {"duplicate object key", arrayOf(replaceOnce(valid, R"("title":"A")",
                                                       R"("title":"A","title":"B")")),
         "duplicate JSON key: title"},
    };

    for (const auto& item : invalidCases) {
        INFO(item.label);
        const TemporaryFixtureFile file(item.json);
        std::string diagnostic;
        try {
            (void)loadFilenameFixtures(file.path());
        } catch (const std::exception& error) {
            diagnostic = error.what();
        }
        REQUIRE_FALSE(diagnostic.empty());
        CHECK(diagnostic.find(item.expectedDiagnostic) != std::string::npos);
    }
}

TEST_CASE("fixture confidence bands follow the parser policy") {
    REQUIRE(confidenceBand(0.0) == ConfidenceBand::low);
    REQUIRE(confidenceBand(0.59) == ConfidenceBand::low);
    REQUIRE(confidenceBand(0.60) == ConfidenceBand::medium);
    REQUIRE(confidenceBand(0.84) == ConfidenceBand::medium);
    REQUIRE(confidenceBand(0.85) == ConfidenceBand::high);
    REQUIRE(confidenceBand(1.0) == ConfidenceBand::high);
    REQUIRE_THROWS_AS(confidenceBand(-0.1), std::invalid_argument);
    REQUIRE_THROWS_AS(confidenceBand(std::numeric_limits<double>::quiet_NaN()),
                      std::invalid_argument);
}
