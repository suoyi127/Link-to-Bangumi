#pragma once

#include "anime_vault/domain/ParseResult.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace anime_vault::test_support {

enum class ConfidenceBand { low, medium, high };

struct FilenameFixture {
    std::string filename;
    std::string title;
    std::optional<std::uint32_t> season;
    std::optional<EpisodeNumber> episode;
    EpisodeType episodeType;
    std::optional<std::string> releaseGroup;
    std::optional<std::string> resolution;
    std::optional<std::uint32_t> version;
    ConfidenceBand confidenceBand;
    std::vector<std::string> warnings;
};

[[nodiscard]] std::vector<FilenameFixture> loadFilenameFixtures(const std::filesystem::path& path);
[[nodiscard]] ConfidenceBand confidenceBand(double confidence);

}  // namespace anime_vault::test_support
