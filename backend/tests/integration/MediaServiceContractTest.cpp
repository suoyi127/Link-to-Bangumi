#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#ifdef ANIME_VAULT_STANDALONE_TEST_MAIN
#include <catch2/catch_session.hpp>
int main(int argc, char* argv[]) { return Catch::Session().run(argc, argv); }
#endif

TEST_CASE("media service scans inbox and previews without creating a target") {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("anime-vault-api-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source = root / "source";
    const auto import = root / "import";
    const auto library = root / "library";
    fs::create_directories(source);
    fs::create_directories(import);
    fs::create_directories(library);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    const auto video = source / "Moonbound - 01.mkv";
    { std::ofstream file(video, std::ios::binary); file << "video"; }
    const auto importedVideo = import / "Imported Moon - 02.mkv";
    { std::ofstream file(importedVideo, std::ios::binary); file << "imported video"; }
    fs::last_write_time(video, fs::file_time_type::clock::now() - std::chrono::minutes(2));
    fs::last_write_time(importedVideo, fs::file_time_type::clock::now() - std::chrono::minutes(2));
    const auto sourceTime = fs::last_write_time(video);
    const auto importTime = fs::last_write_time(importedVideo);
    anime_vault::SqliteDatabase db(root / "vault.db");
    db.migrate();
    anime_vault::SqliteMediaRepository repository(db);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO anime(display_title) VALUES('Moonbound');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {734, "Moonbound", "", "", "", 12, 2});
    anime_vault::api::MediaService service(repository, source, library, import);
    const auto first = service.createScan(std::chrono::seconds{0});
    REQUIRE(first.id > 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const auto second = service.createScan(std::chrono::seconds{0});
    REQUIRE(second.processedCount == 1);
    const auto inbox = service.listInbox();
    REQUIRE(inbox.size() == 1);
    REQUIRE(inbox.front().episodeNumber == "1");
    REQUIRE(inbox.front().bangumiSubjectId == 734);
    REQUIRE(inbox.front().origin == "qb_download");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const auto firstImport = service.createImportScan(std::chrono::seconds{0});
    REQUIRE(firstImport.source == "external_import");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    const auto secondImport = service.createImportScan(std::chrono::seconds{0});
    REQUIRE(secondImport.processedCount == 1);
    const auto combinedInbox = service.listInbox();
    REQUIRE(combinedInbox.size() == 2);
    REQUIRE(combinedInbox.at(0).origin == "qb_download");
    REQUIRE(combinedInbox.at(1).origin == "external_import");
    REQUIRE(fs::file_size(video) == 5);
    REQUIRE(fs::last_write_time(video) == sourceTime);
    REQUIRE(fs::file_size(importedVideo) == 14);
    REQUIRE(fs::last_write_time(importedVideo) == importTime);
    std::ifstream sourceContents(video, std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(sourceContents), {}) == "video");
    std::ifstream importContents(importedVideo, std::ios::binary);
    REQUIRE(std::string(std::istreambuf_iterator<char>(importContents), {}) == "imported video");
    const auto importedPreview = service.preview(combinedInbox.at(1).id);
    REQUIRE(importedPreview.targetPath ==
        (fs::canonical(library) / "Imported Moon" / "Imported Moon [02].mkv").string());
    REQUIRE_FALSE(fs::exists(importedPreview.targetPath));
    anime_vault::api::MediaService missingImport(repository, source, library, root / "missing");
    REQUIRE(missingImport.createScan(std::chrono::seconds{0}).status == "completed");
    try {
        missingImport.createImportScan(std::chrono::seconds{0});
        FAIL("missing import root must fail only on import action");
    } catch (const anime_vault::api::ApiError& error) {
        REQUIRE(error.status == 409);
        REQUIRE(error.code == "import_root_unavailable");
    }
    try {
        anime_vault::api::MediaService overlappingImport(repository, source, library, source);
        FAIL("overlapping import root must be rejected before library creation");
    } catch (const anime_vault::api::ApiError& error) {
        REQUIRE(error.status == 409);
        REQUIRE(error.code == "overlapping_roots");
    }
    try {
        anime_vault::api::MediaService overlappingLibrary(repository, source, source / "new-library", import);
        FAIL("nested library root must be rejected before it is created");
    } catch (const anime_vault::api::ApiError& error) {
        REQUIRE(error.status == 409);
        REQUIRE(error.code == "overlapping_roots");
    }
    REQUIRE(sqlite3_exec(db.handle(),
        "UPDATE anime SET display_title='Canonical Moon' WHERE id=1;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto preview = service.preview(inbox.front().id);
    REQUIRE(preview.id > 0);
    REQUIRE(preview.targetPath ==
        (fs::canonical(library) / "Canonical Moon" / "Canonical Moon [01].mkv").string());
    REQUIRE_FALSE(fs::exists(preview.targetPath));
    const auto copyPreview = service.preview(inbox.front().id, "copy");
    REQUIRE(copyPreview.operation == "copy");
    REQUIRE(repository.getPlan(copyPreview.id)->operation == "copy");
    try {
        (void)service.preview(inbox.front().id, "move");
        FAIL("unsupported operation must be rejected");
    } catch (const anime_vault::api::ApiError& error) {
        REQUIRE(error.code == "invalid_operation");
    }
    fs::create_directories(fs::path(preview.targetPath).parent_path());
    { std::ofstream existing(preview.targetPath); existing << "existing"; }
    const auto conflict = service.preview(inbox.front().id);
    REQUIRE(conflict.id > 0);
    REQUIRE(conflict.conflicts == std::vector<std::string>{"target_exists"});
    REQUIRE(repository.getPlan(conflict.id)->executionState == "conflict:target_exists");
    const auto copyConflict = service.preview(inbox.front().id, "copy");
    REQUIRE(copyConflict.operation == "copy");
    REQUIRE(repository.getPlan(copyConflict.id)->operation == "copy");
    REQUIRE(repository.getPlan(copyConflict.id)->executionState == "conflict:target_exists");
    fs::last_write_time(video, fs::last_write_time(video) + std::chrono::seconds(1));
    REQUIRE_THROWS_AS(service.preview(inbox.front().id), anime_vault::api::ApiError);
    repository.updateMediaCorrection(inbox.front().id, {"", "", "1", "unknown", 0.5, std::nullopt});
    try {
        service.preview(inbox.front().id);
        FAIL("unknown episode type must be rejected");
    } catch (const anime_vault::api::ApiError& error) {
        REQUIRE(error.status == 400);
        REQUIRE(error.code == "invalid_episode_type");
    }
}

TEST_CASE("automatic scan does not turn a remote alias into a user alias") {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
        ("anime-vault-alias-origin-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source = root / "source";
    const auto imported = root / "import";
    const auto library = root / "library";
    fs::create_directories(source);
    fs::create_directories(imported);
    fs::create_directories(library);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    const auto video = source / "Remote Old - 01.mkv";
    { std::ofstream file(video, std::ios::binary); file << "video"; }
    fs::last_write_time(video, fs::file_time_type::clock::now() - std::chrono::minutes(2));
    anime_vault::SqliteDatabase db(root / "vault.db");
    db.migrate();
    anime_vault::SqliteMediaRepository repository(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Local');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {111, "Remote Old", "", "", "", 12, 2});
    anime_vault::api::MediaService service(repository, source, library, imported);
    service.createScan(std::chrono::seconds{0});
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    REQUIRE(service.createScan(std::chrono::seconds{0}).processedCount == 1);
    REQUIRE(service.listInbox().front().bangumiSubjectId == 111);
    repository.bindAnime(1, {222, "Remote New", "", "", "", 12, 2});
    REQUIRE_FALSE(repository.findAnimeByTitle("Remote Old"));
}
