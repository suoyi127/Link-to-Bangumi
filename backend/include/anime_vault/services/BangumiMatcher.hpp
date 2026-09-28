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

// 按标题、年份和集数对搜索结果排序，并判断是否适合无人工确认地绑定。
RankedCandidates rankBangumiCandidates(const BangumiMatchQuery& query,
                                       const std::vector<BangumiSubject>& subjects);

}  // namespace anime_vault
