#pragma once

#include "anime_vault/domain/BangumiMetadata.hpp"

namespace anime_vault {

// 按标题、年份和集数对搜索结果排序，并判断是否适合无人工确认地绑定。
RankedCandidates rankBangumiCandidates(const BangumiMatchQuery& query,
                                       const std::vector<BangumiSubject>& subjects);

}  // namespace anime_vault
