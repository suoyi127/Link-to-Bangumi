#pragma once

#include "anime_vault/domain/MediaRecords.hpp"

namespace anime_vault {

class ScanRepository {
public:
    virtual ~ScanRepository() = default;
    virtual std::int64_t createScan(const ScanRecord& record) = 0;
    virtual FolderImportRecord addFolderImport(const std::string& rootPath) = 0;
    virtual std::vector<FolderImportRecord> listFolderImports() const = 0;
    virtual std::optional<FolderImportRecord> getFolderImport(std::int64_t id) const = 0;
    virtual std::optional<ScanRecord> getScan(std::int64_t id) const = 0;
    virtual void updateScan(const ScanRecord& record) = 0;
    virtual std::int64_t insertMedia(const MediaRecord& record) = 0;
    virtual void markMissingMedia(const std::string& origin,
                                  const std::vector<std::string>& observedPaths,
                                  std::optional<std::int64_t> folderImportId = std::nullopt) = 0;
    virtual std::optional<MediaRecord> getMedia(std::int64_t id) const = 0;
    virtual void updateMediaParse(std::int64_t id, const std::string& episodeNumber,
                                  const std::string& episodeType, double confidence) = 0;
    virtual void updateMediaCorrection(
        std::int64_t id, const MediaCorrection& correction,
        MediaCorrectionIntent intent = MediaCorrectionIntent::userConfirmed) = 0;
    virtual std::vector<ScanRecord> listScans() const = 0;
    virtual ScanPage listScanPage(std::int64_t offset, int limit) const = 0;
    virtual std::vector<MediaRecord> listInbox() const = 0;
    virtual InboxPage listInboxPage(std::int64_t offset, int limit,
                                    std::optional<std::string> origin = std::nullopt) const = 0;
    virtual void updateMediaEpisode(std::int64_t id, const std::string& number, const std::string& type) = 0;
    virtual std::optional<MediaRecord> findMediaBySourcePath(const std::string& path) const = 0;
};

} // namespace anime_vault
