#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace anime_vault {

struct BangumiCandidate {
    std::int64_t id{};
    std::string name, nameCn, date, coverUrl;
    int episodeCount{}, type{};
    double score{};
};

struct RankedCandidates {
    std::vector<BangumiCandidate> items;
    // 最高候选置信度足够且与其他结果有明显差距时才允许自动绑定。
    bool autoBindEligible{};
};

struct BangumiEpisodeMapping { std::string sort, ep; };

struct BangumiSubject {
    std::int64_t id{};
    std::string name, nameCn, date, coverUrl;
    int episodeCount{}, type{};
    std::vector<std::string> aliases;
    std::vector<BangumiEpisodeMapping> episodes;
};

struct BangumiMatchQuery {
    std::string title;
    std::optional<int> year;
    std::optional<int> episodeCount;
};

} // namespace anime_vault
