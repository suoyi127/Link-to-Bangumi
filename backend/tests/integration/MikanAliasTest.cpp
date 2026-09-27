#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <random>

using namespace anime_vault;

namespace {
struct TempDb {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("anime-vault-mikan-" + std::to_string(std::random_device{}()) + ".db");
    ~TempDb() { std::error_code error; std::filesystem::remove(path, error); }
};
}

TEST_CASE("Mikan alias can give an untouched scanned anime its canonical title") {
    TempDb temp;
    SqliteDatabase database(temp.path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('Toumei na Yoru');", nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(repository.applyMikanAlias("Toumei na Yoru", "透明之夜"));
    const auto anime = repository.getAnime(1);
    REQUIRE(anime);
    REQUIRE(anime->displayTitle == "透明之夜");
    REQUIRE(repository.findAnimeByTitle("Toumei na Yoru")->id == 1);
    REQUIRE(repository.findAnimeByTitle("透明之夜")->id == 1);
    REQUIRE_FALSE(repository.applyMikanAlias("Toumei na Yoru", "另一标题"));
    REQUIRE(repository.getAnime(1)->displayTitle == "透明之夜");
}

TEST_CASE("Mikan metadata never overwrites a user-confirmed or bound anime") {
    TempDb temp;
    SqliteDatabase database(temp.path);
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(),
        "INSERT INTO anime(display_title) VALUES('User title');"
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(1,'Roman title','user');"
        "INSERT INTO anime(display_title,bangumi_subject_id) VALUES('Bound roman',123);",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE_FALSE(repository.applyMikanAlias("Roman title", "RSS title"));
    REQUIRE_FALSE(repository.applyMikanAlias("Bound roman", "RSS title"));
    REQUIRE(repository.getAnime(1)->displayTitle == "User title");
    REQUIRE(repository.getAnime(2)->displayTitle == "Bound roman");
}
