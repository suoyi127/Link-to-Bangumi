#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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
    std::optional<std::int64_t> folderImportId;
    std::string rawEpisodeNumber;
    std::string episodeNumberSource{"parsed"};
};

struct FolderImportRecord {
    std::int64_t id{};
    std::string rootPath;
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
    std::string mpvExecutable, qbWebUiUrl, qbDownloadDirectory;
    std::string playerType{"mpv"}, playerExecutable;
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

struct AnimeEdit { std::string displayTitle, season, coverUrl; };

struct AnimeRecord {
    std::int64_t id{};
    std::string displayTitle, originalTitle, season, coverUrl;
    std::optional<int> year;
    std::optional<std::int64_t> bangumiSubjectId;
    bool locked{};
    std::string importedMonth;
    std::vector<std::string> aliases;
    std::vector<MediaRecord> media;
    std::optional<std::int64_t> nextMediaOffset;
};

struct AnimePage {
    std::vector<AnimeRecord> items;
    std::optional<std::int64_t> nextOffset;
};

struct AnimeMergePreview {
    std::string previewId;
    std::int64_t sourceId{}, targetId{}, subjectId{};
    std::string sourceTitle, targetTitle;
    std::int64_t mediaCount{}, aliasCount{}, duplicateEpisodeCount{}, expiresAt{};
};

class AnimeBindingError final : public std::runtime_error {
public:
    explicit AnimeBindingError(std::string code, std::int64_t targetId = 0, std::string title = {})
        : std::runtime_error(code), targetAnimeId(targetId), targetTitle(std::move(title)), code_(std::move(code)) {}
    std::int64_t targetAnimeId{};
    std::string targetTitle;
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

} // namespace anime_vault
