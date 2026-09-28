#pragma once

#include "anime_vault/services/MikanTitleParser.hpp"

#include <cstddef>
#include <functional>

namespace anime_vault {
class MediaRepository;

struct MikanEnrichmentResult {
    std::size_t appliedCount{};
    std::string errorCode;
};

// 将 qB 返回的 Mikan 双语 RSS 标题应用为本地别名，不推断 RSS 未提供的翻译。
class MikanEnricher final {
public:
    using CatalogCompletion = std::function<void(MikanCatalog)>;
    using Fetcher = std::function<void(CatalogCompletion)>;
    using Completion = std::function<void(MikanEnrichmentResult)>;
    MikanEnricher(MediaRepository& repository, Fetcher fetcher);
    void runOnce(Completion completion = {}) const;
private:
    MediaRepository& repository_;
    Fetcher fetcher_;
};
}
