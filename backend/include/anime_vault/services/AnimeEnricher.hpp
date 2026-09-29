#pragma once

#include "anime_vault/services/BangumiService.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace anime_vault {
class MediaRepository;
class CoverScraper;
struct AnimeRecord;

// 将扫描到的本地标题匹配到 Bangumi；RSS 文章标题只用于明确双语别名，不替代文件名。
class AnimeEnricher final : public std::enable_shared_from_this<AnimeEnricher> {
public:
    using Completion = std::function<void(std::size_t)>;
    AnimeEnricher(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                  std::shared_ptr<CoverScraper> covers = {});
    // 仅尝试处理有限数量的未绑定条目，减少外部请求并保留歧义给用户确认。
    void runOnce(std::size_t limit = 5, Completion completion = {});
    // 文件夹扫描只处理本次发现的番剧，不让既有未匹配条目占满全局批次。
    void runForScan(std::int64_t scanId, Completion completion = {});
private:
    void runPending(std::vector<AnimeRecord> pending, Completion completion);
    MediaRepository& repository_;
    std::shared_ptr<BangumiService> bangumi_;
    std::shared_ptr<CoverScraper> covers_;
};
}
