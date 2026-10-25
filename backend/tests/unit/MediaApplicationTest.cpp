#include "anime_vault/application/MediaService.hpp"
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace anime_vault;
namespace {
class MemoryRepository final : public MediaRepository {
public:
    std::vector<MediaRecord> media;
    ScanRecord scan;
    std::int64_t createScan(const ScanRecord& record) override { scan = record; scan.id = 1; return 1; }
    FolderImportRecord addFolderImport(const std::string& ) override { return {}; }
    std::vector<FolderImportRecord> listFolderImports() const override { return {}; }
    std::optional<FolderImportRecord> getFolderImport(std::int64_t ) const override { return {}; }
    std::optional<ScanRecord> getScan(std::int64_t id) const override { return id == scan.id ? std::optional<ScanRecord>(scan) : std::nullopt; }
    void updateScan(const ScanRecord& record) override { scan = record; }
    std::int64_t insertMedia(const MediaRecord& record) override { media.push_back(record); media.back().id = static_cast<std::int64_t>(media.size()); return media.back().id; }
    void markMissingMedia(const std::string& , const std::vector<std::string>& , std::optional<std::int64_t> ) override {  }
    std::optional<MediaRecord> getMedia(std::int64_t id) const override { for (const auto& item : media) if (item.id == id) return item; return std::nullopt; }
    void updateMediaParse(std::int64_t , const std::string& , const std::string& , double ) override {  }
    void updateMediaCorrection( std::int64_t , const MediaCorrection& , MediaCorrectionIntent ) override {  }
    std::int64_t insertPlan(const PlanRecord& ) override { return {}; }
    std::optional<PlanRecord> getPlan(std::int64_t ) const override { return {}; }
    std::optional<PlanRecord> findPlanByIdempotencyKey(const std::string& ) const override { return {}; }
    OrganizationJobRecord claimOrganization(std::int64_t , const std::string& ) override { return {}; }
    std::optional<OrganizationJobRecord> findOrganizationJobByKey(const std::string& ) const override { return {}; }
    std::optional<OrganizationJobRecord> getOrganizationJob(std::int64_t ) const override { return {}; }
    void completeOrganization(std::int64_t , const std::string& , std::int64_t ) override {  }
    void failOrganization(std::int64_t , const std::string& ) override {  }
    std::vector<ScanRecord> listScans() const override { return {}; }
    ScanPage listScanPage(std::int64_t , int ) const override { return {}; }
    AuditPage listAuditPage(std::int64_t , int , std::optional<std::int64_t> ) const override { return {}; }
    UiPreferences getUiPreferences() const override { return {}; }
    void putUiPreferences(const UiPreferences& ) override {  }
    std::vector<MediaRecord> listInbox() const override { return {}; }
    InboxPage listInboxPage(std::int64_t , int , std::optional<std::string> ) const override { return {}; }
    std::optional<BangumiCacheRecord> getBangumiCache(const std::string& ) const override { return {}; }
    void putBangumiCache(const BangumiCacheRecord& ) override {  }
    bool putBangumiFailureIfStale(const BangumiCacheRecord& , std::int64_t ) override { return {}; }
    std::vector<AnimeRecord> listAnime() const override { return {}; }
    AnimePage listAnimePage(std::int64_t , int , const std::string& ) const override { return {}; }
    AnimeRecord editAnime(std::int64_t , const AnimeEdit& ) override { return {}; }
    void updateMediaEpisode(std::int64_t , const std::string& , const std::string& ) override {  }
    std::optional<MediaRecord> findMediaBySourcePath(const std::string& ) const override { return {}; }
    std::vector<AnimeRecord> listAnimeForScan(std::int64_t ) const override { return {}; }
    std::optional<AnimeRecord> getAnime(std::int64_t , std::int64_t , int ) const override { return {}; }
    std::optional<AnimeRecord> findAnimeByBangumiSubject(std::int64_t) const override { return {}; }
    AnimeRecord bindAnime(std::int64_t , const BangumiSubject& , bool ) override { return {}; }
    AnimeMergePreview previewAnimeMerge(std::int64_t, std::int64_t) override { return {}; }
    AnimeRecord mergeAnime(std::int64_t, const std::string&) override { return {}; }
    std::optional<AnimeRecord> nextAnimeNeedingEpisodeMapping(std::int64_t) const override { return {}; }
    bool updateEpisodeMappingsIfBound(std::int64_t, const BangumiSubject&) override { return {}; }
    bool updateCoverIfBound(std::int64_t , std::int64_t , const std::string& , std::optional<std::string> ) override { return {}; }
    bool applyMikanAlias(const std::string& , const std::string& ) override { return {}; }
    std::optional<AnimeRecord> findAnimeByTitle( const std::string& , std::optional<std::string> ) const override { return {}; }
};
struct TemporaryRoots {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("media-application-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryRoots() { for (const auto* name : {"source", "library", "import"}) std::filesystem::create_directories(root / name); }
    ~TemporaryRoots() { std::error_code error; std::filesystem::remove_all(root, error); }
};
}
TEST_CASE("media application scans through an injectable persistence port without HTTP") {
    TemporaryRoots roots;
    { std::ofstream video(roots.root / "source" / "Series - 01.mkv"); video << "video"; }
    MemoryRepository repository;
    MediaService service(repository, roots.root / "source", roots.root / "library", roots.root / "import");
    const auto scan = service.createScan(std::chrono::seconds{0});
    REQUIRE(scan.status == "completed");
    REQUIRE(scan.processedCount == 1);
    REQUIRE(repository.media.size() == 1);
    REQUIRE(repository.media.front().episodeNumber == "1");
    REQUIRE(repository.media.front().origin == "qb_download");
    REQUIRE_FALSE(repository.media.front().sourceModifiedAt.empty());
}
TEST_CASE("media application preserves failure categories independently of HTTP") {
    TemporaryRoots roots;
    MemoryRepository repository;
    MediaService service(repository, roots.root / "source", roots.root / "library", roots.root / "import", false);
    const auto failure = [](const auto& action, MediaFailureKind kind, const std::string& code) {
        try { action(); FAIL("expected application failure"); }
        catch (const MediaError& error) { CHECK(error.kind == kind); CHECK(error.code == code); }
    };
    failure([&] { service.getScan(0); }, MediaFailureKind::invalidInput, "invalid_id");
    failure([&] { service.getScan(99); }, MediaFailureKind::missingResource, "scan_not_found");
    failure([&] { service.createFolderScan(99); }, MediaFailureKind::missingResource, "folder_import_not_found");
    failure([&] { service.validateQbSourceReady(); }, MediaFailureKind::conflict, "qb_download_dir_unconfigured");
    const auto video = roots.root / "source" / "Series - 01.mkv";
    { std::ofstream file(video); file << "video"; }
    MediaRecord imported;
    imported.id = 1;
    const auto sourceBytes = video.u8string();
    imported.sourcePath = std::string(reinterpret_cast<const char*>(sourceBytes.data()), sourceBytes.size());
    imported.title = "Series";
    imported.episodeNumber = "1";
    imported.episodeType = "normal";
    imported.origin = "folder_import";
    imported.folderImportId = 99;
    repository.media.push_back(imported);
    failure([&] { service.preview(1); }, MediaFailureKind::conflict, "folder_import_not_found");
}
