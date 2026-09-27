#include "anime_vault/services/RuntimeWebRoot.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;

TEST_CASE("production web root requires an isolated absolute frontend build") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-web-root-" + std::to_string(std::random_device{}()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{root};
    const auto web = root / "web";
    const auto outside = root / "outside";
    fs::create_directories(web);
    fs::create_directories(outside);
    REQUIRE_THROWS(anime_vault::validatedWebRoot("relative-web"));
    REQUIRE_THROWS(anime_vault::validatedWebRoot(root / "missing"));
    REQUIRE_THROWS(anime_vault::validatedWebRoot(web));
    { std::ofstream file(web / "index.html"); file << "ok"; }
    REQUIRE(anime_vault::validatedWebRoot(web) == fs::canonical(web));
    fs::remove(web / "index.html");
    { std::ofstream file(outside / "index.html"); file << "outside"; }
    std::error_code linkError;
    fs::create_symlink(outside / "index.html", web / "index.html", linkError);
    if (!linkError) REQUIRE_THROWS(anime_vault::validatedWebRoot(web));
}
