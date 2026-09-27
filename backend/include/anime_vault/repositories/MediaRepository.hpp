#pragma once

#include "anime_vault/services/BangumiMatcher.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace anime_vault {

struct ScanRecord {
    std::int64_t id{};
    std::string source;
    std::string status;
    std::int64_t discoveredCount{};
    std::int64_t processedCount{};
    std::int64_t errorCount{};
    std::string errorSummary;
};

struct MediaRecord {
    std::int64_t id{};
    std::int64_t scanId{};
    std::string sourcePath;
    std::string filename;
    std::string episodeNumber;
    std::string episodeType;
    std::int64_t sizeBytes{};
    std::string status;
    double confidence{};
    std::string title;
    std::string season;
    std::optional<std::int64_t> bangumiSubjectId;
    std::string sourceModifiedAt;
    std::string origin{"qb_download"};
    std::optional<std::int64_t> animeId;
    std::string parsedTitle;
    std::string libraryPath;
};

struct ScanPage { std::vector<ScanRecord> items; std::optional<std::int64_t> nextOffset; };
struct InboxPage { std::vector<MediaRecord> items; std::optional<std::int64_t> nextOffset; std::int64_t total{}; };
struct AuditRecord {
    std::int64_t id{};
    std::string action, entityType, entityId, createdAt;
};
struct AuditPage { std::vector<AuditRecord> items; std::optional<std::int64_t> nextOffset; };
struct UiPreferences {
    std::string preferredOperation{"hardlink"};
    int scanIntervalSeconds{3600};
    std::string mpvExecutable, qbWebUiUrl;
};

struct MediaCorrection {
    std::string title;
    std::string season;
    std::string episodeNumber;
    std::string episodeType;
    double confidence{};
    std::optional<std::int64_t> bangumiSubjectId;
};

enum class MediaCorrectionIntent { userConfirmed, automaticScan };

struct PlanRecord {
    std::int64_t id{};
    std::int64_t mediaFileId{};
    std::string sourcePath;
    std::int64_t sourceSize{};
    std::string sourceModifiedAt;
    std::string targetPath;
    std::string operation;
    std::string expiresAt;
    std::string executionState;
    std::string idempotencyKey;
};

struct OrganizationJobRecord {
    std::int64_t id{};
    std::int64_t planId{};
    std::string idempotencyKey;
    std::string status;
    std::string resultPath;
    std::string failureCode;
    std::int64_t bytes{};
    bool newlyClaimed{};
};

struct BangumiCacheRecord {
    std::string queryKey;
    std::string responseJson;
    std::int64_t expiresAt{};
    int retryCount{};
    std::optional<std::int64_t> retryAfter;
};

struct AnimeRecord {
    std::int64_t id{};
    std::string displayTitle, originalTitle, season, coverUrl;
    std::optional<int> year;
    std::optional<std::int64_t> bangumiSubjectId;
    bool locked{};
    std::vector<std::string> aliases;
    std::vector<MediaRecord> media;
    std::optional<std::int64_t> nextMediaOffset;
};

struct AnimePage {
    std::vector<AnimeRecord> items;
    std::optional<std::int64_t> nextOffset;
};

class AnimeBindingError final : public std::runtime_error {
public:
    explicit AnimeBindingError(std::string code)
        : std::runtime_error(code), code_(std::move(code)) {}
    const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

class OrganizationClaimError final : public std::runtime_error {
public:
    explicit OrganizationClaimError(std::string code)
        : std::runtime_error(code), code_(std::move(code)) {}
    const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

class MediaRepository {
public:
    virtual ~MediaRepository() = default;
    virtual std::int64_t createScan(const ScanRecord& record) = 0;
    virtual std::optional<ScanRecord> getScan(std::int64_t id) const = 0;
    virtual void updateScan(const ScanRecord& record) = 0;
    virtual std::int64_t insertMedia(const MediaRecord& record) = 0;
    virtual void markMissingMedia(const std::string& origin,
                                  const std::vector<std::string>& observedPaths) = 0;
    virtual std::optional<MediaRecord> getMedia(std::int64_t id) const = 0;
    virtual void updateMediaParse(std::int64_t id, const std::string& episodeNumber,
                                  const std::string& episodeType, double confidence) = 0;
    virtual void updateMediaCorrection(
        std::int64_t id, const MediaCorrection& correction,
        MediaCorrectionIntent intent = MediaCorrectionIntent::userConfirmed) = 0;
    virtual std::int64_t insertPlan(const PlanRecord& record) = 0;
    virtual std::optional<PlanRecord> getPlan(std::int64_t id) const = 0;
    virtual std::optional<PlanRecord> findPlanByIdempotencyKey(const std::string& key) const = 0;
    virtual OrganizationJobRecord claimOrganization(std::int64_t planId, const std::string& key) = 0;
    virtual std::optional<OrganizationJobRecord> findOrganizationJobByKey(const std::string& key) const = 0;
    virtual std::optional<OrganizationJobRecord> getOrganizationJob(std::int64_t id) const = 0;
    virtual void completeOrganization(std::int64_t jobId, const std::string& resultPath,
                                      std::int64_t bytes) = 0;
    virtual void failOrganization(std::int64_t jobId, const std::string& code) = 0;
    virtual std::vector<ScanRecord> listScans() const = 0;
    virtual ScanPage listScanPage(std::int64_t offset, int limit) const = 0;
    virtual AuditPage listAuditPage(std::int64_t offset, int limit,
                                    std::optional<std::int64_t> animeId = std::nullopt) const = 0;
    virtual UiPreferences getUiPreferences() const = 0;
    virtual void putUiPreferences(const UiPreferences& preferences) = 0;
    virtual std::vector<MediaRecord> listInbox() const = 0;
    virtual InboxPage listInboxPage(std::int64_t offset, int limit) const = 0;
    virtual std::optional<BangumiCacheRecord> getBangumiCache(const std::string& key) const = 0;
    virtual void putBangumiCache(const BangumiCacheRecord& record) = 0;
    virtual bool putBangumiFailureIfStale(const BangumiCacheRecord& record,
                                         std::int64_t requestStartedAt) = 0;
    virtual std::vector<AnimeRecord> listAnime() const = 0;
    virtual AnimePage listAnimePage(std::int64_t offset, int limit) const = 0;
    virtual std::optional<AnimeRecord> getAnime(std::int64_t id,
                                                std::int64_t mediaOffset = 0,
                                                int mediaLimit = 50) const = 0;
    virtual AnimeRecord bindAnime(std::int64_t id, const BangumiSubject& subject) = 0;
    virtual bool updateCoverIfBound(std::int64_t animeId, std::int64_t subjectId,
                                    const std::string& localUrl) = 0;
    virtual bool applyMikanAlias(const std::string& alias,
                                 const std::string& canonicalTitle) = 0;
    virtual std::optional<AnimeRecord> findAnimeByTitle(
        const std::string& title, std::optional<std::string> season = std::nullopt) const = 0;
};

} // namespace anime_vault
