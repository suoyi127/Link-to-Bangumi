#pragma once

#include "anime_vault/domain/MediaRecords.hpp"
#include "anime_vault/domain/BangumiMetadata.hpp"

namespace anime_vault {

class AnimeRepository {
public:
    virtual ~AnimeRepository() = default;
    virtual std::vector<AnimeRecord> listAnime() const = 0;
    virtual AnimePage listAnimePage(std::int64_t offset, int limit, const std::string& sort = "import_desc") const = 0;
    virtual AnimeRecord editAnime(std::int64_t id, const AnimeEdit& edit) = 0;
    virtual std::vector<AnimeRecord> listAnimeForScan(std::int64_t scanId) const = 0;
    virtual std::optional<AnimeRecord> getAnime(std::int64_t id,
                                                std::int64_t mediaOffset = 0,
                                                int mediaLimit = 50) const = 0;
    virtual std::optional<AnimeRecord> findAnimeByBangumiSubject(std::int64_t) const { return std::nullopt; }
    virtual AnimeRecord bindAnime(std::int64_t id, const BangumiSubject& subject,
                                  bool canonicalizeTitle = false) = 0;
    virtual AnimeMergePreview previewAnimeMerge(std::int64_t, std::int64_t) { throw AnimeBindingError("merge_unavailable"); }
    virtual AnimeRecord mergeAnime(std::int64_t, const std::string&) { throw AnimeBindingError("merge_unavailable"); }
    virtual std::optional<AnimeRecord> nextAnimeNeedingEpisodeMapping(std::int64_t) const { return std::nullopt; }
    virtual bool updateEpisodeMappingsIfBound(std::int64_t, const BangumiSubject&) { return false; }
    virtual bool updateCoverIfBound(std::int64_t animeId, std::int64_t subjectId,
                                    const std::string& localUrl, std::optional<std::string> expectedCover = std::nullopt) = 0;
    virtual bool applyMikanAlias(const std::string& alias,
                                 const std::string& canonicalTitle) = 0;
    virtual std::optional<AnimeRecord> findAnimeByTitle(
        const std::string& title, std::optional<std::string> season = std::nullopt) const = 0;
};

} // namespace anime_vault
