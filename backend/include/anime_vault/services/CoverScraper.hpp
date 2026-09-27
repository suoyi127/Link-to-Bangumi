#pragma once

#include "anime_vault/services/BangumiService.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace anime_vault {
class MediaRepository;

class CoverImageFetcher {
public:
    using Completion = std::function<void(std::optional<std::string>, std::string)>;
    virtual ~CoverImageFetcher() = default;
    virtual void fetch(std::string url, Completion completion) = 0;
};

struct CoverScrapeResult {
    std::string coverUrl;
    std::string errorCode;
};

struct CachedCover {
    std::string bytes;
    std::string mimeType;
    std::string extension;
};

class CoverScraper final : public std::enable_shared_from_this<CoverScraper> {
public:
    using Completion = std::function<void(CoverScrapeResult)>;
    CoverScraper(MediaRepository& repository, std::shared_ptr<BangumiService> bangumi,
                 CoverImageFetcher& fetcher, std::filesystem::path dataRoot);
    void refresh(std::int64_t animeId, Completion completion);
    void cacheSubject(std::int64_t animeId, BangumiSubject subject, Completion completion = {});
    std::optional<CachedCover> readCached(std::int64_t animeId, std::int64_t subjectId,
                                           std::string_view filename) const;
    static std::optional<std::string> allowedImagePath(std::string_view url);
    static std::optional<CachedCover> detectImage(std::string_view bytes);
private:
    MediaRepository& repository_;
    std::shared_ptr<BangumiService> bangumi_;
    CoverImageFetcher& fetcher_;
    std::filesystem::path dataRoot_;
};
}
