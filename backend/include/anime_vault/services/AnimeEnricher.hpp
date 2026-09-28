#pragma once

#include "anime_vault/services/BangumiService.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace anime_vault {
class MediaRepository;
class CoverScraper;

// 将扫描到的本地标题匹配到 Bangumi；RSS 文章标题只用于明确双语别名，不替代文件名。
class AnimeEnricher final : public std::enable_shared_from_this<AnimeEnricher> {
public:
    using Completion = std::function<void(std::size_t)>;
    AnimeEnricher(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                  std::shared_ptr<CoverScraper> covers = {});
    // 仅尝试处理有限数量的未绑定条目，减少外部请求并保留歧义给用户确认。
    void runOnce(std::size_t limit = 5, Completion completion = {});
private:
    MediaRepository& repository_;
    std::shared_ptr<BangumiService> bangumi_;
    std::shared_ptr<CoverScraper> covers_;
};
}
