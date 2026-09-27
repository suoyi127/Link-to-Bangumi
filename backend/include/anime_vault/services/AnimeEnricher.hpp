#pragma once

#include "anime_vault/services/BangumiService.hpp"

#include <cstddef>
#include <functional>
#include <memory>

namespace anime_vault {
class MediaRepository;
class CoverScraper;

// Resolves scanned local titles to Bangumi without trusting RSS article titles as filenames.
class AnimeEnricher final : public std::enable_shared_from_this<AnimeEnricher> {
public:
    using Completion = std::function<void(std::size_t)>;
    AnimeEnricher(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                  std::shared_ptr<CoverScraper> covers = {});
    void runOnce(std::size_t limit = 5, Completion completion = {});
private:
    MediaRepository& repository_;
    std::shared_ptr<BangumiService> bangumi_;
    std::shared_ptr<CoverScraper> covers_;
};
}
