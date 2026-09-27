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
    bool autoBindEligible{};
};

struct BangumiSubject {
    std::int64_t id{};
    std::string name, nameCn, date, coverUrl;
    int episodeCount{}, type{};
};

struct BangumiMatchQuery {
    std::string title;
    std::optional<int> year;
    std::optional<int> episodeCount;
};

RankedCandidates rankBangumiCandidates(const BangumiMatchQuery& query,
                                       const std::vector<BangumiSubject>& subjects);

}  // namespace anime_vault
