#include "anime_vault/services/RuntimePaths.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <random>

namespace fs = std::filesystem;

TEST_CASE("runtime paths leave qB unconfigured until the user selects a directory") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-runtime-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    fs::create_directories(root / "data");
    fs::create_directories(root / "qb");
    fs::create_directories(root / "override");
    const auto unconfigured = anime_vault::resolveRuntimePaths(
        root / "data", root / "import", root / "library", {}, "");
    REQUIRE_FALSE(unconfigured.qbConfigured);
    REQUIRE(unconfigured.source == root / "data" / "unconfigured-qb-source");
    const auto stored = anime_vault::resolveRuntimePaths(
        root / "data", root / "import", root / "library", {}, (root / "qb").string());
    REQUIRE(stored.qbConfigured);
    REQUIRE(stored.source == root / "qb");
    const auto overridden = anime_vault::resolveRuntimePaths(
        root / "data", root / "import", root / "library", root / "override", (root / "qb").string());
    REQUIRE(overridden.source == root / "override");
}

TEST_CASE("qB directory preference survives reopening the settings database") {
    const auto path = fs::temp_directory_path() /
        ("anime-vault-runtime-settings-" + std::to_string(std::random_device{}()) + ".db");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove(path, error); } } cleanup{path};
    {
        anime_vault::SqliteDatabase db(path);
        db.migrate();
        anime_vault::SqliteMediaRepository repository(db);
        auto preferences = repository.getUiPreferences();
        preferences.qbDownloadDirectory = "C:/Downloads/Anime";
        repository.putUiPreferences(preferences);
    }
    anime_vault::SqliteDatabase db(path);
    db.migrate();
    anime_vault::SqliteMediaRepository repository(db);
    REQUIRE(repository.getUiPreferences().qbDownloadDirectory == "C:/Downloads/Anime");
}
