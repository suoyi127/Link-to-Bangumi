#include "anime_vault/services/MikanEnricher.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <random>

using namespace anime_vault;

TEST_CASE("Mikan enrichment adopts a matching RSS alias after a local anime exists") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-enrichment-" + std::to_string(std::random_device{}()) + ".db");
    {
        SqliteDatabase database(path);
        database.migrate();
        SqliteMediaRepository repository(database);
        REQUIRE(sqlite3_exec(database.handle(),
            "INSERT INTO anime(display_title) VALUES('Roman Title');", nullptr, nullptr, nullptr) == SQLITE_OK);
        MikanEnricher enricher(repository, [](MikanEnricher::CatalogCompletion completion) {
            completion({1, 1, {{"中文标题", "Roman Title", 3}}, ""});
        });
        bool done = false;
        enricher.runOnce([&](MikanEnrichmentResult result) {
            done = true;
            REQUIRE(result.appliedCount == 1);
            REQUIRE(result.errorCode.empty());
        });
        REQUIRE(done);
        REQUIRE(repository.getAnime(1)->displayTitle == "中文标题");
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}
