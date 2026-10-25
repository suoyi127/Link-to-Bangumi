#pragma once

#include "anime_vault/repositories/MediaRepository.hpp"
#include "anime_vault/services/DirectoryScanner.hpp"
#include "anime_vault/services/FilenameParser.hpp"
#include "anime_vault/services/TargetPlanner.hpp"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <memory>
#include "anime_vault/domain/MediaError.hpp"

namespace anime_vault {

struct PreviewResponse {
    std::int64_t id{};
    std::string targetPath;
    std::string operation;
    std::vector<std::string> conflicts;
    std::string expiresAt;
};

struct MediaEpisodeEdit { std::string episodeNumber, episodeType; };

class MediaService {
public:
    MediaService(MediaRepository& repository, std::filesystem::path sourceRoot,
                 std::filesystem::path libraryRoot,
                 std::filesystem::path importRoot = "D:/追番/外来导入",
                 bool qbConfigured = true);
    ScanRecord createCompletedScan(const std::vector<std::filesystem::path>& completedPaths);
    ScanRecord createScan(std::chrono::seconds stableFor = std::chrono::seconds{60});
    void validateQbSourceReady() const;
    ScanRecord createImportScan(std::chrono::seconds stableFor = std::chrono::seconds{60});
    FolderImportRecord addFolderImport(const std::string& path);
    std::vector<FolderImportRecord> listFolderImports() const;
    ScanRecord createFolderScan(std::int64_t id);
    ScanRecord getScan(std::int64_t id) const;
    std::vector<MediaRecord> listInbox() const;
    InboxPage listInboxPage(std::int64_t offset, int limit,
                            std::optional<std::string> origin = std::nullopt) const;
    MediaRecord editEpisode(std::int64_t id, const MediaEpisodeEdit& edit);
    MediaRecord correct(std::int64_t id, const MediaCorrection& correction);
    PreviewResponse preview(std::int64_t id, const std::string& operation = "hardlink");

private:
    MediaRepository& repository_;
    bool qbConfigured_;
    std::filesystem::path sourceRoot_;
    std::filesystem::path sourceRequestedRoot_;
    std::filesystem::path importRoot_;
    std::filesystem::path importScannerRoot_;
    std::filesystem::path libraryRoot_;
    DirectoryScanner scanner_;
    std::unique_ptr<DirectoryScanner> importScanner_;
    TargetPlanner planner_;
    FilenameParser parser_;
    std::chrono::steady_clock::time_point lastScan_{};
    mutable std::mutex mutex_;
    ScanRecord scanFrom(DirectoryScanner& scanner, const std::string& origin,
                        std::chrono::seconds stableFor,
                        std::optional<std::int64_t> folderImportId = std::nullopt,
                        const std::vector<SourceFile>* verifiedFiles = nullptr);
    void enforceRateLimit();
    void validateSourceRoot() const;
};

} // namespace anime_vault
