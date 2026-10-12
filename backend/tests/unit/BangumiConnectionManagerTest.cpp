#include "anime_vault/infrastructure/network/BangumiConnectionManager.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>

using namespace anime_vault;

TEST_CASE("episode lookup passes through the configured Bangumi connection") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-episode-connection-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    SqliteDatabase db(path); db.migrate();
    BangumiConnectionManager manager(db, "");
    std::string error;
    manager.episodes(548156, 0, [&](auto, auto code) { error = code; });
    REQUIRE(error == "bangumi_unconfigured");
    manager.save("suoyi127/Link-to-Bangumi/0.1 (Windows)");
    manager.episodes(-1, 0, [&](auto, auto code) { error = code; });
    REQUIRE(error == "invalid_episode_query");
}

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
