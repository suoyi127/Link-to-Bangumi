#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <filesystem>
#include <random>
#include <functional>
#include <algorithm>
#include <cstring>

using namespace anime_vault;

static std::string bindingCode(const std::function<void()>& action) {
    try { action(); }
    catch (const AnimeBindingError& error) { return error.code(); }
    return "";
}

TEST_CASE("explicit binding is transactional and local anime remains readable offline") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-binding-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Local A'),('Local B'),('Locked');"
        "UPDATE anime SET locked=1 WHERE id=3;", nullptr, nullptr, nullptr) == SQLITE_OK);
    const BangumiSubject subject{123, "Frieren", "葬送的芙莉莲", "2023-09-29", "https://example.invalid/cover.jpg", 28, 2};
    const auto bound = repository.bindAnime(1, subject);
    REQUIRE(bound.id == 1);
    REQUIRE(bound.bangumiSubjectId == 123);
    REQUIRE(bound.displayTitle == "Local A");
    REQUIRE(bound.originalTitle == "Frieren");
    REQUIRE(bound.coverUrl == subject.coverUrl);
    REQUIRE(bound.year == 2023);
    REQUIRE(bound.aliases.size() == 2);
    REQUIRE(repository.getAnime(1)->bangumiSubjectId == 123);
    REQUIRE(repository.listAnime().size() == 3);
    REQUIRE(bindingCode([&] { repository.bindAnime(2, subject); }) == "bangumi_subject_in_use");
    REQUIRE_FALSE(repository.getAnime(2)->bangumiSubjectId);
    REQUIRE(bindingCode([&] { repository.bindAnime(3, subject); }) == "anime_locked");
    REQUIRE(bindingCode([&] { repository.bindAnime(0, subject); }) == "invalid_id");
    REQUIRE(bindingCode([&] { repository.bindAnime(1, BangumiSubject{}); }) == "invalid_subject");
    REQUIRE(repository.getAnime(1)->bangumiSubjectId == 123);

    const auto scan = repository.createScan({0, "test", "completed"});
    const auto media = repository.insertMedia({0, scan, "C:/test/episode.mkv", "episode.mkv", "1", "normal", 1, "inbox", 1.0});
    repository.updateMediaCorrection(media, {"Offline title", "1", "1", "normal", 1.0, std::nullopt});
    REQUIRE(repository.listAnime().size() == 4);
    REQUIRE(repository.getAnime(4)->displayTitle == "Offline title");
    REQUIRE(repository.getAnime(4)->media.size() == 1);
    const auto linked = repository.insertMedia({0, scan, "C:/test/linked.mkv", "linked.mkv", "2", "normal", 1, "inbox", 1.0});
    REQUIRE(sqlite3_exec(database.handle(),
        ("UPDATE media_file SET anime_id=1 WHERE id=" + std::to_string(linked)).c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.updateMediaCorrection(linked, {"Local A", "", "2", "normal", 1.0, std::nullopt});
    REQUIRE(repository.listAnime().size() == 4);
    REQUIRE(repository.getAnime(1)->media.size() == 1);
    REQUIRE(repository.getMedia(linked)->bangumiSubjectId == 123);
}

TEST_CASE("cover cache update only applies to the currently bound Bangumi subject") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-cover-binding-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Local');", nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {123, "Old", "", "", "https://lain.bgm.tv/old.jpg", 12, 2});
    REQUIRE(repository.updateCoverIfBound(1, 123, "/api/covers/1/123/token.jpg"));
    REQUIRE(repository.getAnime(1)->coverUrl == "/api/covers/1/123/token.jpg");
    repository.bindAnime(1, {456, "New", "", "", "", 12, 2});
    REQUIRE_FALSE(repository.updateCoverIfBound(1, 123, "/api/covers/1/123/stale.jpg"));
    REQUIRE(repository.getAnime(1)->coverUrl.empty());
}

TEST_CASE("anime and detail media pages expose records beyond default limits") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-pages-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<201) "
        "INSERT INTO anime(display_title) SELECT printf('Anime %d',x) FROM n;"
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<501) "
        "INSERT INTO media_file(anime_id,source_path,filename,size_bytes,status) "
        "SELECT 1,printf('C:/test/%d.mkv',x),printf('%d.mkv',x),1,'inbox' FROM n;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto first = repository.listAnimePage(0, 100);
    REQUIRE(first.items.size() == 100);
    REQUIRE(first.nextOffset == 100);
    const auto third = repository.listAnimePage(200, 100);
    REQUIRE(third.items.size() == 1);
    REQUIRE(third.items.front().id == 1);
    REQUIRE_FALSE(third.nextOffset);
    const auto firstMedia = repository.getAnime(1, 0, 100);
    REQUIRE(firstMedia->media.size() == 100);
    REQUIRE(firstMedia->nextMediaOffset == 100);
    const auto lastMedia = repository.getAnime(1, 500, 100);
    REQUIRE(lastMedia->media.size() == 1);
    REQUIRE_FALSE(lastMedia->nextMediaOffset);
}

TEST_CASE("rebinding replaces remote metadata and aliases without removing user aliases") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-rebind-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Local');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {123, "Old title", "旧标题", "2023-09-29",
        "https://example.invalid/old.jpg", 12, 2});
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(1,'my alias','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto rebound = repository.bindAnime(1, {456, "New title", "新标题", "",
        "", 24, 2});
    REQUIRE(rebound.bangumiSubjectId == 456);
    REQUIRE(rebound.originalTitle == "New title");
    REQUIRE(rebound.coverUrl.empty());
    REQUIRE_FALSE(rebound.year);
    REQUIRE(rebound.aliases.size() == 3);
    REQUIRE(std::find(rebound.aliases.begin(), rebound.aliases.end(), "my alias") != rebound.aliases.end());
    REQUIRE(std::find(rebound.aliases.begin(), rebound.aliases.end(), "oldtitle") == rebound.aliases.end());
    REQUIRE(std::find(rebound.aliases.begin(), rebound.aliases.end(), "newtitle") != rebound.aliases.end());
    REQUIRE(sqlite3_exec(database.handle(),
        "UPDATE anime SET locked=1 WHERE id=1;", nullptr, nullptr, nullptr) == SQLITE_OK);
    const auto lockedSame = repository.bindAnime(1, {456, "Different metadata", "", "2025-01-01",
        "https://example.invalid/different.jpg", 24, 2});
    REQUIRE(lockedSame.originalTitle == "New title");
    REQUIRE(lockedSame.coverUrl.empty());
    REQUIRE_FALSE(lockedSame.year);
}

TEST_CASE("unique local aliases link corrected media and ambiguous aliases do not") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-local-alias-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('葬送的芙莉莲'),('Other');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {123, "Sousou no Frieren", "葬送的芙莉莲", "", "", 28, 2});
    REQUIRE(repository.findAnimeByTitle("葬送 的 芙莉莲")->id == 1);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(1,'Legacy Alias','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(repository.findAnimeByTitle("legacyalias")->id == 1);
    const auto scan = repository.createScan({0, "test", "completed"});
    const auto media = repository.insertMedia({0, scan, "C:/test/local.mkv", "local.mkv", "1", "normal", 1, "inbox", 1.0});
    repository.updateMediaCorrection(media, {"葬送 的 芙莉莲", "", "1", "normal", 1.0, {}});
    REQUIRE(repository.getMedia(media)->bangumiSubjectId == 123);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(2,'葬送的芙莉莲','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE_FALSE(repository.findAnimeByTitle("葬送的芙莉莲"));
    const auto ambiguous = repository.insertMedia({0, scan, "C:/test/ambiguous.mkv", "ambiguous.mkv", "2", "normal", 1, "inbox", 1.0});
    repository.updateMediaCorrection(ambiguous, {"葬送的芙莉莲", "", "2", "normal", 1.0, {}});
    REQUIRE_FALSE(repository.getMedia(ambiguous)->bangumiSubjectId);
    repository.updateMediaCorrection(media, {"Renamed title", "", "1", "normal", 1.0, {}});
    REQUIRE(repository.getMedia(media)->bangumiSubjectId == 123);
}

TEST_CASE("a corrected second season never attaches to a first-season alias") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-seasons-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title,season) VALUES('Show','1');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {111, "Show", "", "", "", 12, 2});
    const auto scan = repository.createScan({0, "test", "completed"});
    const auto secondSeason = repository.insertMedia({0, scan, "C:/test/s2.mkv", "s2.mkv", "1", "normal", 1, "inbox", 1.0});
    repository.updateMediaCorrection(secondSeason, {"Show", "2", "1", "normal", 1.0, {}});
    REQUIRE_FALSE(repository.getMedia(secondSeason)->bangumiSubjectId);
    REQUIRE(repository.getAnime(2)->season == "2");
    REQUIRE_FALSE(repository.findAnimeByTitle("Show"));
    REQUIRE(repository.findAnimeByTitle("Show", "1")->id == 1);
    REQUIRE(repository.findAnimeByTitle("Show", "2")->id == 2);
    const auto firstSeason = repository.insertMedia({0, scan, "C:/test/s1.mkv", "s1.mkv", "2", "normal", 1, "inbox", 1.0});
    repository.updateMediaCorrection(firstSeason, {"Show", "1", "2", "normal", 1.0, {}});
    REQUIRE(repository.getMedia(firstSeason)->bangumiSubjectId == 111);
}

TEST_CASE("explicitly corrected Bangumi alias survives later rebinding") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-promote-alias-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Local');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {111, "Remote Old", "", "", "", 12, 2});
    const auto scan = repository.createScan({0, "test", "completed"});
    const auto media = repository.insertMedia({0, scan, "C:/test/alias.mkv", "alias.mkv", "1", "normal", 1, "inbox", 1.0});
    repository.updateMediaCorrection(media, {"Remote Old", "", "1", "normal", 1.0, {}});
    repository.bindAnime(1, {222, "Remote New", "", "", "", 12, 2});
    const auto previous = repository.findAnimeByTitle("Remote Old");
    REQUIRE(previous);
    REQUIRE(previous->id == 1);
    REQUIRE(repository.getAnime(1)->bangumiSubjectId == 222);
}

TEST_CASE("local lookup reuses its index during many scan corrections and refreshes after rebind") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-index-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
        std::error_code error; std::filesystem::remove(path, error);
    } } cleanup{path};
    SqliteDatabase database(path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Local');"
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(1,'Legacy Alias','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repository.bindAnime(1, {111, "Old Remote", "", "", "", 12, 2});
    int fullScans = 0;
    sqlite3_trace_v2(database.handle(), SQLITE_TRACE_STMT,
        [](unsigned, void* context, void*, void* sql) -> int {
            if (std::strstr(static_cast<const char*>(sql), "UNION ALL"))
                ++*static_cast<int*>(context);
            return 0;
        }, &fullScans);
    const auto scan = repository.createScan({0, "test", "completed"});
    for (int i = 0; i < 60; ++i) {
        const auto pathText = "C:/test/episode-" + std::to_string(i) + ".mkv";
        const auto media = repository.insertMedia({0, scan, pathText, "episode.mkv", "1", "normal", 1, "inbox", 1.0});
        repository.updateMediaCorrection(media, {"Legacy Alias", "", "1", "normal", 1.0, {}},
            MediaCorrectionIntent::automaticScan);
        REQUIRE(repository.getMedia(media)->bangumiSubjectId == 111);
    }
    for (int i = 0; i < 2; ++i) {
        const auto pathText = "C:/test/new-series-" + std::to_string(i) + ".mkv";
        const auto media = repository.insertMedia({0, scan, pathText, "new.mkv", "1", "normal", 1, "inbox", 1.0});
        repository.updateMediaCorrection(media, {"New Series", "", "1", "normal", 1.0, {}},
            MediaCorrectionIntent::automaticScan);
    }
    REQUIRE(repository.getAnime(2)->media.size() == 2);
    REQUIRE(fullScans <= 2);
    repository.bindAnime(1, {222, "New Remote", "", "", "", 12, 2});
    REQUIRE(repository.findAnimeByTitle("New Remote"));
    REQUIRE(repository.findAnimeByTitle("legacyalias"));
    REQUIRE(fullScans <= 3);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Other');"
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(2,'legacy alias','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE_FALSE(repository.findAnimeByTitle("Legacy Alias"));
    sqlite3_trace_v2(database.handle(), 0, nullptr, nullptr);
}
