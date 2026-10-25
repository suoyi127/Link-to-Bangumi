#pragma once

#include "anime_vault/domain/MediaRecords.hpp"

namespace anime_vault {

class MetadataCache {
public:
    virtual ~MetadataCache() = default;
    virtual std::optional<BangumiCacheRecord> getBangumiCache(const std::string& key) const = 0;
    virtual void putBangumiCache(const BangumiCacheRecord& record) = 0;
    virtual bool putBangumiFailureIfStale(const BangumiCacheRecord& record,
                                         std::int64_t requestStartedAt) = 0;
};

} // namespace anime_vault
