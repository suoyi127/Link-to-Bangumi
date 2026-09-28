#include "anime_vault/infrastructure/network/BangumiConnectionManager.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>

using namespace anime_vault;

TEST_CASE("Bangumi configuration persists and clears to environment fallback") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-bangumi-connection-" + std::to_string(std::random_device{}()) + ".db");
    {
        SqliteDatabase db(path); db.migrate();
        BangumiConnectionManager manager(db, "developer/App/1.0");
        REQUIRE(manager.summary().source == "environment");
        manager.save("suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)");
        REQUIRE(manager.summary().source == "saved");
        REQUIRE_THROWS_AS(manager.save("bad\r\nInjected: true"), BangumiConfigError);
    }
    {
        SqliteDatabase db(path); db.migrate();
        BangumiConnectionManager manager(db, "developer/App/1.0");
        REQUIRE(manager.summary().source == "saved");
        manager.clear();
        REQUIRE(manager.summary().source == "environment");
        REQUIRE(manager.summary().userAgent == "developer/App/1.0");
    }
    std::filesystem::remove(path);
}
