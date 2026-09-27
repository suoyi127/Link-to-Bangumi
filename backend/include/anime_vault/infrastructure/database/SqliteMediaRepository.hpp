#pragma once

#include "anime_vault/repositories/MediaRepository.hpp"
#include <unordered_map>

namespace anime_vault {
class SqliteDatabase;
std::optional<std::int64_t> boundedInboxNextOffset(std::int64_t offset, int limit, bool hasMore);

class SqliteMediaRepository final : public MediaRepository {
public:
    explicit SqliteMediaRepository(SqliteDatabase& database) : database_(database) {}
    std::int64_t createScan(const ScanRecord& record) override;
    std::optional<ScanRecord> getScan(std::int64_t id) const override;
    void updateScan(const ScanRecord& record) override;
    std::int64_t insertMedia(const MediaRecord& record) override;
    void markMissingMedia(const std::string& origin,
                          const std::vector<std::string>& observedPaths) override;
    std::optional<MediaRecord> getMedia(std::int64_t id) const override;
    void updateMediaParse(std::int64_t id, const std::string& episodeNumber,
                          const std::string& episodeType, double confidence) override;
    void updateMediaCorrection(
        std::int64_t id, const MediaCorrection& correction,
        MediaCorrectionIntent intent = MediaCorrectionIntent::userConfirmed) override;
    std::int64_t insertPlan(const PlanRecord& record) override;
    std::optional<PlanRecord> getPlan(std::int64_t id) const override;
    std::optional<PlanRecord> findPlanByIdempotencyKey(const std::string& key) const override;
    OrganizationJobRecord claimOrganization(std::int64_t planId, const std::string& key) override;
    std::optional<OrganizationJobRecord> findOrganizationJobByKey(const std::string& key) const override;
    std::optional<OrganizationJobRecord> getOrganizationJob(std::int64_t id) const override;
    void completeOrganization(std::int64_t jobId, const std::string& resultPath,
                              std::int64_t bytes) override;
    void failOrganization(std::int64_t jobId, const std::string& code) override;
    std::vector<ScanRecord> listScans() const override;
    ScanPage listScanPage(std::int64_t offset, int limit) const override;
    AuditPage listAuditPage(std::int64_t offset, int limit,
                            std::optional<std::int64_t> animeId = std::nullopt) const override;
    UiPreferences getUiPreferences() const override;
    void putUiPreferences(const UiPreferences& preferences) override;
    std::vector<MediaRecord> listInbox() const override;
    InboxPage listInboxPage(std::int64_t offset, int limit,
                            std::optional<std::string> origin = std::nullopt) const override;
    std::optional<BangumiCacheRecord> getBangumiCache(const std::string& key) const override;
    void putBangumiCache(const BangumiCacheRecord& record) override;
    bool putBangumiFailureIfStale(const BangumiCacheRecord& record,
                                 std::int64_t requestStartedAt) override;
    std::vector<AnimeRecord> listAnime() const override;
    AnimePage listAnimePage(std::int64_t offset, int limit) const override;
    std::optional<AnimeRecord> getAnime(std::int64_t id, std::int64_t mediaOffset = 0,
                                        int mediaLimit = 50) const override;
    AnimeRecord bindAnime(std::int64_t id, const BangumiSubject& subject) override;
    bool updateCoverIfBound(std::int64_t animeId, std::int64_t subjectId,
                            const std::string& localUrl) override;
    bool applyMikanAlias(const std::string& alias,
                         const std::string& canonicalTitle) override;
    std::optional<AnimeRecord> findAnimeByTitle(
        const std::string& title, std::optional<std::string> season = std::nullopt) const override;
private:
    SqliteDatabase& database_;
    // Guarded by SqliteDatabase::mutex(); refreshed when another writer changes
    // the connection, and incrementally extended during scan corrections.
    mutable std::int64_t titleIndexChanges_{-1};
    mutable int titleIndexDataVersion_{-1};
    mutable std::unordered_map<std::string, std::vector<AnimeRecord>> titleIndex_;
};

} // namespace anime_vault
