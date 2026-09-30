#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"
#include "anime_vault/services/OrganizationService.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>

using namespace anime_vault;
namespace fs = std::filesystem;
namespace {
std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
}

TEST_CASE("confirmed organization is durable, idempotent and provenance-aware") {
    const auto root = fs::canonical(fs::temp_directory_path()) /
        ("anime-vault-organization-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto qb = root / "qb";
    const auto imported = root / "import";
    const auto library = root / "library";
    fs::create_directories(qb);
    fs::create_directories(imported);
    fs::create_directories(library);
    const auto source = qb / "Show 01.mkv";
    { std::ofstream file(source, std::ios::binary); file << "original episode"; }
    const auto target = library / "Show" / "01.mkv";
    const auto sourceBytes = fs::file_size(source);
    const auto modified = fs::last_write_time(source);
    SqliteDatabase db(root / "test.db");
    db.migrate();
    REQUIRE(db.schemaVersion() == 10);
    SqliteMediaRepository repo(db);
    const auto scan = repo.createScan({0, "qb_download", "completed"});
    MediaRecord media{0, scan, utf8(source), utf8(source.filename()), "1", "normal",
                      static_cast<std::int64_t>(sourceBytes), "inbox", 1.0};
    media.sourceModifiedAt = std::to_string(modified.time_since_epoch().count());
    const auto mediaId = repo.insertMedia(media);
    const auto planId = repo.insertPlan({0, mediaId, utf8(source),
        static_cast<std::int64_t>(sourceBytes), media.sourceModifiedAt, utf8(target),
        "hardlink", "2099-01-01T00:00:00Z", "pending", "preview-1"});
    OrganizationService service(repo, qb, imported, library);
    const ExecuteOrganizationRequest request{planId, "execution-1", true, true};
    auto rejected = [&](const ExecuteOrganizationRequest& command, const std::string& code) {
        try { (void)service.execute(command); FAIL("expected organization rejection"); }
        catch (const OrganizationServiceError& error) { REQUIRE(error.code() == code); }
    };
    rejected({planId, "execution-1", true, false}, "qb_completion_required");
    rejected({planId, "execution-1", false, true}, "confirmation_required");
    REQUIRE_FALSE(fs::exists(target));
    const auto first = service.execute(request);
    REQUIRE(first.jobId > 0);
    REQUIRE(first.targetPath == utf8(target));
    REQUIRE(first.bytes == static_cast<std::int64_t>(sourceBytes));
    REQUIRE(first.status == "completed");
    REQUIRE(fs::exists(source));
    REQUIRE(fs::exists(target));
    REQUIRE(fs::file_size(source) == sourceBytes);
    const auto replay = service.execute(request);
    REQUIRE(replay.jobId == first.jobId);
    REQUIRE(repo.getOrganizationJob(first.jobId)->status == "completed");
    // A successful key is durable even if a later scan changes the source snapshot.
    auto rescanned = media;
    rescanned.sizeBytes += 1;
    rescanned.sourceModifiedAt = "changed-by-rescan";
    REQUIRE(repo.insertMedia(rescanned) == mediaId);
    const auto replayAfterRescan = service.execute(request);
    REQUIRE(replayAfterRescan.jobId == first.jobId);
    REQUIRE(service.execute({planId, "execution-1", true, false}).jobId == first.jobId);
    REQUIRE(repo.insertMedia(media) == mediaId);
    rejected({planId, "execution-2", true, true}, "plan_already_claimed");
    const auto sibling = repo.insertPlan({0, mediaId, utf8(source),
        static_cast<std::int64_t>(sourceBytes), media.sourceModifiedAt,
        utf8(library / "Show" / "05.mkv"), "hardlink", "2099-01-01T00:00:00Z",
        "pending", "preview-sibling"});
    rejected({sibling, "execution-sibling", true, true}, "plan_already_claimed");
    REQUIRE_FALSE(fs::exists(library / "Show" / "05.mkv"));
    const auto expired = repo.insertPlan({0, mediaId, utf8(source),
        static_cast<std::int64_t>(sourceBytes), media.sourceModifiedAt,
        utf8(library / "Show" / "02.mkv"), "hardlink", "2000-01-01T00:00:00Z",
        "pending", "preview-expired"});
    rejected({expired, "execution-1", true, true}, "idempotency_key_reused");
    rejected({expired, "execution-expired", true, true}, "plan_expired");
    const auto conflicted = repo.insertPlan({0, mediaId, utf8(source),
        static_cast<std::int64_t>(sourceBytes), media.sourceModifiedAt,
        utf8(library / "Show" / "03.mkv"), "hardlink", "2099-01-01T00:00:00Z",
        "conflict:target_exists", "preview-conflict"});
    rejected({conflicted, "execution-conflict", true, true}, "plan_conflict");
    const auto secondSource = qb / "Show 02.mkv";
    { std::ofstream file(secondSource, std::ios::binary); file << "another episode"; }
    MediaRecord secondMedia = media;
    secondMedia.id = 0;
    secondMedia.sourcePath = utf8(secondSource);
    secondMedia.filename = utf8(secondSource.filename());
    secondMedia.sizeBytes = static_cast<std::int64_t>(fs::file_size(secondSource));
    secondMedia.sourceModifiedAt = std::to_string(fs::last_write_time(secondSource).time_since_epoch().count());
    const auto secondMediaId = repo.insertMedia(secondMedia);
    const auto interrupted = repo.insertPlan({0, secondMediaId, utf8(secondSource),
        secondMedia.sizeBytes, secondMedia.sourceModifiedAt,
        utf8(library / "Show" / "04.mkv"), "hardlink", "2099-01-01T00:00:00Z",
        "pending", "preview-interrupted"});
    const auto claim = repo.claimOrganization(interrupted, "execution-interrupted");
    REQUIRE(claim.status == "running");
    const auto competing = repo.insertPlan({0, secondMediaId, utf8(secondSource),
        secondMedia.sizeBytes, secondMedia.sourceModifiedAt,
        utf8(library / "Show" / "06.mkv"), "hardlink", "2099-01-01T00:00:00Z",
        "pending", "preview-competing"});
    rejected({competing, "execution-competing", true, true}, "plan_already_claimed");
    rejected({interrupted, "execution-interrupted", true, true}, "execution_in_progress");
    REQUIRE_FALSE(fs::exists(library / "Show" / "04.mkv"));

    const auto invalidSource = imported / "Invalid 01.mkv";
    { std::ofstream file(invalidSource, std::ios::binary); file << "import bytes"; }
    MediaRecord invalidMedia = media;
    invalidMedia.id = 0;
    invalidMedia.sourcePath = utf8(invalidSource);
    invalidMedia.filename = utf8(invalidSource.filename());
    invalidMedia.sizeBytes = static_cast<std::int64_t>(fs::file_size(invalidSource));
    invalidMedia.sourceModifiedAt = "not-a-timestamp";
    invalidMedia.origin = "external_import";
    const auto invalidMediaId = repo.insertMedia(invalidMedia);
    const auto invalidPlanId = repo.insertPlan({0, invalidMediaId, utf8(invalidSource),
        invalidMedia.sizeBytes, invalidMedia.sourceModifiedAt,
        utf8(library / "Invalid" / "01.mkv"), "hardlink", "2099-01-01T00:00:00Z",
        "pending", "preview-invalid-timestamp"});
    rejected({invalidPlanId, "execution-invalid-timestamp", true, false}, "invalid_plan");
    const auto invalidJob = repo.findOrganizationJobByKey("execution-invalid-timestamp");
    REQUIRE(invalidJob);
    REQUIRE(invalidJob->status == "failed");
    REQUIRE(invalidJob->failureCode == "invalid_plan");
    REQUIRE(fs::exists(invalidSource));
    REQUIRE_FALSE(fs::exists(library / "Invalid" / "01.mkv"));

    const auto importSource = imported / "Import 01.mkv";
    { std::ofstream file(importSource, std::ios::binary); file << "import bytes"; }
    auto importMedia = invalidMedia;
    importMedia.sourcePath = utf8(importSource);
    importMedia.filename = utf8(importSource.filename());
    importMedia.sizeBytes = static_cast<std::int64_t>(fs::file_size(importSource));
    importMedia.sourceModifiedAt =
        std::to_string(fs::last_write_time(importSource).time_since_epoch().count());
    const auto importMediaId = repo.insertMedia(importMedia);
    const auto importTarget = library / "Import" / "01.mkv";
    const auto importPlanId = repo.insertPlan({0, importMediaId, utf8(importSource),
        importMedia.sizeBytes, importMedia.sourceModifiedAt, utf8(importTarget),
        "hardlink", "2099-01-01T00:00:00Z", "pending", "preview-import"});
    REQUIRE(service.execute({importPlanId, "execution-import", true, false}).status == "completed");
    REQUIRE(fs::exists(importSource));
    REQUIRE(fs::exists(importTarget));
}

TEST_CASE("folder-import media organizes from its registered root without qB confirmation") {
    const auto root = fs::canonical(fs::temp_directory_path()) /
        ("anime-vault-folder-organize-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto qb = root / "qb", imported = root / "import", folder = root / "folder", library = root / "library";
    for (const auto& path : {qb, imported, folder, library}) fs::create_directories(path);
    const auto source = folder / "Show 01.mkv";
    { std::ofstream file(source); file << "episode"; }
    SqliteDatabase db(root / "vault.db");
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto registration = repo.addFolderImport(utf8(fs::canonical(folder)));
    const auto scan = repo.createScan({0, "folder_import", "completed"});
    MediaRecord media{0, scan, utf8(fs::canonical(source)), utf8(source.filename()), "1", "normal",
                      static_cast<std::int64_t>(fs::file_size(source)), "inbox", 1};
    media.origin = "folder_import";
    media.folderImportId = registration.id;
    media.sourceModifiedAt = std::to_string(fs::last_write_time(source).time_since_epoch().count());
    const auto id = repo.insertMedia(media);
    const auto target = library / "Show" / "Show [01].mkv";
    REQUIRE(fs::canonical(library) == library);
    REQUIRE(fs::canonical(folder) == folder);
    const auto plan = repo.insertPlan({0, id, media.sourcePath, media.sizeBytes, media.sourceModifiedAt,
        utf8(target), "hardlink", "2099-01-01T00:00:00Z", "pending", "folder-preview"});
    OrganizationService service(repo, qb, imported, library);
    const auto result = service.execute({plan, "folder-execution", true, false});
    REQUIRE(result.status == "completed");
    REQUIRE(fs::exists(source));
    REQUIRE(fs::exists(target));
}
