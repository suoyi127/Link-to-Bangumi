#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <future>
#include <mutex>
#include <random>
#include <chrono>
#include <sqlite3.h>

using namespace anime_vault;

TEST_CASE("inbox continuation never exceeds the accepted offset range") {
    REQUIRE(boundedInboxNextOffset(999'900, 100, true) == 1'000'000);
    REQUIRE_FALSE(boundedInboxNextOffset(1'000'000, 100, true));
    REQUIRE_FALSE(boundedInboxNextOffset(0, 100, false));
}

TEST_CASE("folder imports have a separate inbox origin") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-folder-origin-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    SqliteDatabase db(path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(repo.listInboxPage(0, 100, "folder_import").total == 0);
    const auto first = repo.addFolderImport("D:/folder-one");
    const auto second = repo.addFolderImport("D:/folder-two");
    REQUIRE(first.id != second.id);
    REQUIRE(repo.addFolderImport("D:/folder-one").id == first.id);
    REQUIRE(repo.listFolderImports().size() == 2);
    const auto scan = repo.createScan({0, "folder_import", "completed"});
    MediaRecord one{0, scan, "D:/folder-one/01.mkv", "01.mkv", "01", "normal", 10, "inbox", 1};
    one.origin = "folder_import";
    one.folderImportId = first.id;
    const auto oneId = repo.insertMedia(one);
    MediaRecord two{0, scan, "D:/folder-two/02.mkv", "02.mkv", "02", "normal", 10, "inbox", 1};
    two.origin = "folder_import";
    two.folderImportId = second.id;
    const auto twoId = repo.insertMedia(two);
    REQUIRE(repo.getMedia(oneId)->origin == "folder_import");
    REQUIRE(repo.getMedia(twoId)->folderImportId == second.id);
    REQUIRE(repo.listInboxPage(0, 100, "folder_import").total == 2);
    REQUIRE(repo.listInboxPage(0, 100, "external_import").total == 0);
    repo.markMissingMedia("folder_import", {}, first.id);
    REQUIRE(repo.getMedia(oneId)->status == "missing");
    REQUIRE(repo.getMedia(twoId)->status == "inbox");
}

TEST_CASE("scan and inbox records survive reopening a migrated database") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-sqlite-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    {
        SqliteDatabase db(path);
        db.migrate();
        REQUIRE(db.schemaVersion() == 10);
        SqliteMediaRepository repo(db);
        const auto scanId = repo.createScan({0, "D:/sample", "running", 1, 0, 0, ""});
        const auto mediaId = repo.insertMedia({0, scanId, "D:/sample/01.mkv", "01.mkv", "1.5", "normal", 1234, "inbox", 0.9});
        REQUIRE(mediaId > 0);
        REQUIRE(repo.getMedia(mediaId)->origin == "qb_download");
        REQUIRE(repo.insertMedia({0, scanId, "D:/sample/01.mkv", "01.mkv", "1.5", "normal", 1234, "inbox", 0.9}) == mediaId);
        const auto otherScanId = repo.createScan({0, "D:/external", "running", 1, 0, 0, ""});
        MediaRecord contradictory{0, otherScanId, "D:/sample/01.mkv", "01.mkv", "1.5", "normal", 9999, "inbox", 0.9};
        contradictory.origin = "external_import";
        contradictory.sourceModifiedAt = "different-snapshot";
        REQUIRE_THROWS_AS(repo.insertMedia(contradictory), std::invalid_argument);
        const auto unchanged = repo.getMedia(mediaId);
        REQUIRE(unchanged->origin == "qb_download");
        REQUIRE(unchanged->scanId == scanId);
        REQUIRE(unchanged->sizeBytes == 1234);
        REQUIRE(unchanged->sourceModifiedAt.empty());
        MediaRecord external{0, otherScanId, "D:/sample/external.mkv", "external.mkv", "2", "normal", 456, "inbox", 0.8};
        external.parsedTitle = "External Parsed";
        external.origin = "external_import";
        external.sourceModifiedAt = "original-external";
        const auto externalId = repo.insertMedia(external);
        REQUIRE(repo.getMedia(externalId)->origin == "external_import");
        REQUIRE(repo.getMedia(externalId)->parsedTitle == "External Parsed");
        auto reverseContradiction = external;
        reverseContradiction.origin = "qb_download";
        reverseContradiction.scanId = scanId;
        reverseContradiction.sizeBytes = 9999;
        reverseContradiction.sourceModifiedAt = "different-snapshot";
        REQUIRE_THROWS_AS(repo.insertMedia(reverseContradiction), std::invalid_argument);
        const auto externalUnchanged = repo.getMedia(externalId);
        REQUIRE(externalUnchanged->origin == "external_import");
        REQUIRE(externalUnchanged->scanId == otherScanId);
        REQUIRE(externalUnchanged->sizeBytes == 456);
        REQUIRE(externalUnchanged->sourceModifiedAt == "original-external");
        external.origin = "unexpected";
        REQUIRE_THROWS_AS(repo.insertMedia(external), std::invalid_argument);
        REQUIRE(repo.getMedia(externalId)->origin == "external_import");
        REQUIRE(repo.getScan(scanId)->id == scanId);
        repo.updateScan({scanId, "D:/sample", "completed", 1, 1, 0, ""});
        repo.updateMediaParse(mediaId, "2.5", "normal", 1.0);
        repo.updateMediaCorrection(mediaId, {"Moonbound", "2026 Autumn", "2.5", "normal", 1.0, 12345});
        REQUIRE(repo.getMedia(mediaId)->episodeNumber == "2.5");
        REQUIRE(repo.getMedia(mediaId)->title == "Moonbound");
        REQUIRE(repo.getMedia(mediaId)->bangumiSubjectId == 12345);
        const auto planId = repo.insertPlan({0, mediaId, "D:/sample/01.mkv", 1234, "snapshot", "D:/library/02.mkv", "hardlink", "2099-01-01", "pending", "once"});
        REQUIRE(repo.getPlan(planId)->id == planId);
        REQUIRE(repo.findPlanByIdempotencyKey("once")->id == planId);
        REQUIRE(repo.insertPlan({0, mediaId, "D:/sample/01.mkv", 1234, "snapshot", "D:/library/02.mkv", "hardlink", "2099-01-01", "pending", "once"}) == planId);
    }
    {
        SqliteDatabase db(path);
        db.migrate();
        REQUIRE(db.schemaVersion() == 10);
        SqliteMediaRepository repo(db);
        const auto scans = repo.listScans();
        const auto media = repo.listInbox();
        REQUIRE(scans.size() == 2);
        REQUIRE(scans.front().source == "D:/sample");
        REQUIRE(scans.front().status == "completed");
        REQUIRE(media.size() == 2);
        REQUIRE(media.front().origin == "qb_download");
        REQUIRE(media.back().origin == "external_import");
        REQUIRE(media.front().episodeNumber == "2.5");
        REQUIRE(media.front().season == "2026 Autumn");
        REQUIRE(media.front().bangumiSubjectId == 12345);
        REQUIRE(media.front().scanId == scans.front().id);
    }
}

TEST_CASE("schema migration six backfills parsed titles from existing corrections") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-migration-six-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    SqliteDatabase db(path);
    db.migrate();
    auto* handle = db.handle();
    REQUIRE(sqlite3_exec(handle, "INSERT INTO scan_job(source,status) VALUES('test','completed');"
        "INSERT INTO media_file(scan_id,source_path,filename,episode_number,episode_type,size_bytes,status,confidence,title) "
        "VALUES(1,'D:/old.mkv','old.mkv','1','normal',1,'inbox',1.0,'Previously Corrected');"
        "INSERT INTO media_file(scan_id,source_path,filename,episode_number,episode_type,size_bytes,status,confidence) "
        "VALUES(1,'D:/unparsed.mkv','unparsed.mkv','','unknown',1,'inbox',0.0);"
        "DROP INDEX media_file_folder_import;"
        "ALTER TABLE media_file DROP COLUMN folder_import_id;"
        "DROP TABLE folder_import;"
        "DROP TABLE game_resource; DROP TABLE novel_file; DROP TABLE novel_work; DROP TABLE novel_source;"
        "ALTER TABLE media_file DROP COLUMN parsed_title; PRAGMA user_version=5;", nullptr, nullptr, nullptr) == SQLITE_OK);
    db.migrate();
    REQUIRE(db.schemaVersion() == 10);
    SqliteMediaRepository repo(db);
    const auto media = repo.getMedia(1);
    REQUIRE(media);
    REQUIRE(media->parsedTitle == "Previously Corrected");
    const auto unparsed = repo.getMedia(2);
    REQUIRE(unparsed);
    REQUIRE(unparsed->parsedTitle.empty());
}

TEST_CASE("user correction records title aliases and automatic scans never rename canonical anime") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-title-alias-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    SqliteDatabase db(path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto scanId = repo.createScan({0, "D:/sample", "completed"});
    MediaRecord first{0, scanId, "D:/sample/01.mkv", "01.mkv", "1", "normal", 1, "inbox", 1.0};
    first.parsedTitle = "Toumei na Yoru";
    const auto firstId = repo.insertMedia(first);
    repo.updateMediaCorrection(firstId, {"Toumei na Yoru", "", "1", "normal", 1.0, std::nullopt},
                               MediaCorrectionIntent::automaticScan);
    const auto initial = repo.getMedia(firstId);
    REQUIRE(initial);
    const auto animeId = initial->animeId;
    REQUIRE(animeId);

    repo.updateMediaCorrection(firstId, {"透明夜之恋", "", "1", "normal", 1.0, std::nullopt},
                               MediaCorrectionIntent::userConfirmed);
    auto anime = repo.getAnime(*animeId);
    REQUIRE(anime);
    REQUIRE(anime->displayTitle == "透明夜之恋");
    const auto oldAlias = repo.findAnimeByTitle("Toumei na Yoru");
    REQUIRE(oldAlias);
    REQUIRE(oldAlias->id == *animeId);

    MediaRecord later{0, scanId, "D:/sample/02.mkv", "02.mkv", "2", "normal", 1, "inbox", 1.0};
    later.parsedTitle = "Toumei na Yoru";
    const auto laterId = repo.insertMedia(later);
    repo.updateMediaCorrection(laterId, {"Toumei na Yoru", "", "2", "normal", 1.0, std::nullopt},
                               MediaCorrectionIntent::automaticScan);
    const auto attached = repo.getMedia(laterId);
    REQUIRE(attached);
    REQUIRE(attached->animeId == animeId);
    REQUIRE(repo.getAnime(*animeId)->displayTitle == "透明夜之恋");
    const auto canonical = repo.findAnimeByTitle("透明夜之恋");
    REQUIRE(canonical);
    REQUIRE(canonical->id == *animeId);
}

TEST_CASE("confirmed binding to another anime does not transfer the prior canonical alias") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-title-rebind-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    SqliteDatabase db(path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto scanId = repo.createScan({0, "D:/sample", "completed"});
    MediaRecord a{0, scanId, "D:/sample/a.mkv", "a.mkv", "1", "normal", 1, "inbox", 1.0};
    a.parsedTitle = "Parsed A";
    const auto aId = repo.insertMedia(a);
    repo.updateMediaCorrection(aId, {"Anime A", "", "1", "normal", 1.0, std::nullopt},
                               MediaCorrectionIntent::automaticScan);
    const auto animeA = repo.getMedia(aId)->animeId;
    REQUIRE(animeA);
    repo.updateMediaCorrection(aId, {"Anime A", "", "1", "normal", 1.0, std::nullopt},
                               MediaCorrectionIntent::userConfirmed);
    const auto parsedAliasA = repo.findAnimeByTitle("Parsed A");
    REQUIRE(parsedAliasA);
    REQUIRE(parsedAliasA->id == *animeA);

    MediaRecord b{0, scanId, "D:/sample/b.mkv", "b.mkv", "1", "normal", 1, "inbox", 1.0};
    b.parsedTitle = "Parsed B";
    const auto bId = repo.insertMedia(b);
    repo.updateMediaCorrection(bId, {"Anime B", "", "1", "normal", 1.0, 700},
                               MediaCorrectionIntent::userConfirmed);
    const auto animeB = repo.getMedia(bId)->animeId;
    REQUIRE(animeB);
    REQUIRE(*animeA != *animeB);

    repo.updateMediaCorrection(aId, {"Anime B", "", "1", "normal", 1.0, 700},
                               MediaCorrectionIntent::userConfirmed);
    const auto aTitleLookup = repo.findAnimeByTitle("Anime A");
    REQUIRE(aTitleLookup);
    REQUIRE(aTitleLookup->id == *animeA);
    const auto parsedLookup = repo.findAnimeByTitle("Parsed A");
    REQUIRE(parsedLookup);
    REQUIRE(parsedLookup->id == *animeA);
    const auto canonicalB = repo.findAnimeByTitle("Anime B");
    REQUIRE(canonicalB);
    REQUIRE(canonicalB->id == *animeB);
}

TEST_CASE("repository calls share the database connection lock") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-connection-lock-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    SqliteDatabase db(path);
    db.migrate();
    SqliteMediaRepository first(db);
    SqliteMediaRepository second(db);
    std::future<std::int64_t> write;
    bool blocked{};
    {
        std::unique_lock lock(db.mutex());
        std::promise<void> ready;
        auto started = ready.get_future();
        write = std::async(std::launch::async, [&] {
            ready.set_value();
            return second.createScan({0, "D:/sample", "running"});
        });
        started.wait();
        blocked = write.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    }
    REQUIRE(blocked);
    const auto scanId = write.get();
    REQUIRE(first.getScan(scanId)->id == scanId);
}

TEST_CASE("title index observes alias commits from another SQLite connection") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-external-title-write-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Existing Anime');", nullptr, nullptr, nullptr) == SQLITE_OK);
    SqliteMediaRepository repository(database);
    REQUIRE_FALSE(repository.findAnimeByTitle("External Alias"));

    SqliteDatabase otherConnection(path);
    REQUIRE(sqlite3_exec(otherConnection.handle(),
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(1,'externalalias','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);

    const auto lookup = repository.findAnimeByTitle("External Alias");
    REQUIRE(lookup);
    REQUIRE(lookup->id == 1);
}
