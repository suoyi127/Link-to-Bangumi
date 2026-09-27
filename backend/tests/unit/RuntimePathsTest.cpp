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

TEST_CASE("qB directory selection accepts only an independent existing directory") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-path-validation-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    fs::create_directories(root / "qb");
    fs::create_directories(root / "import");
    fs::create_directories(root / "library");
    fs::create_directories(root / "data");
    REQUIRE(anime_vault::validateQbDownloadDirectory(root / "qb", root / "import",
        root / "library", root / "data") == fs::canonical(root / "qb"));
    REQUIRE_THROWS(anime_vault::validateQbDownloadDirectory("relative", root / "import",
        root / "library", root / "data"));
    REQUIRE_THROWS(anime_vault::validateQbDownloadDirectory(root / "missing", root / "import",
        root / "library", root / "data"));
    REQUIRE_THROWS(anime_vault::validateQbDownloadDirectory(root / "import", root / "import",
        root / "library", root / "data"));
    REQUIRE_THROWS(anime_vault::validateQbDownloadDirectory(root, root / "import",
        root / "library", root / "data"));
    REQUIRE_THROWS(anime_vault::validateQbDownloadDirectory(root.root_path(), root / "import",
        root / "library", root / "data"));
}

TEST_CASE("a newly saved qB directory cannot receive downloads before restart") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-pending-source-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    fs::create_directories(root / "active");
    fs::create_directories(root / "pending");
    const auto pendingBytes = (root / "pending").u8string();
    const std::string pending(reinterpret_cast<const char*>(pendingBytes.data()), pendingBytes.size());
    REQUIRE_FALSE(anime_vault::qbDirectoryActive(root / "active", pending, false));
    REQUIRE(anime_vault::qbDirectoryActive(root / "active", pending, true));
    REQUIRE(anime_vault::qbDirectoryActive(root / "pending", pending, false));
    REQUIRE_FALSE(anime_vault::qbDirectoryActive(root / "active", "", false));
    REQUIRE(anime_vault::qbDirectoryActive(root / "active", "", true));
}
