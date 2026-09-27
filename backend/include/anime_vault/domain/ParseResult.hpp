#pragma once

#include "anime_vault/domain/EpisodeNumber.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace anime_vault {

enum class EpisodeType { normal, sp, ova, ncop, nced, unknown };

struct ParseResult {
    std::string originalName;
    std::string title;
    std::optional<std::uint32_t> season;
    std::optional<EpisodeNumber> episode;
    EpisodeType episodeType{EpisodeType::unknown};
    std::optional<std::string> releaseGroup;
    std::optional<std::string> resolution;
    std::optional<std::uint32_t> version;
    double confidence{};
    std::vector<std::string> warnings;
};

}  // namespace anime_vault
