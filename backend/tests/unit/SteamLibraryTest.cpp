#include "anime_vault/services/SteamLibrary.hpp"
#include "anime_vault/domain/Game.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <chrono>

namespace fs = std::filesystem;
using anime_vault::SteamLibrary;
namespace {
struct Fixture {
    fs::path base = fs::temp_directory_path() / ("steam-library-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path root = base / "Steam";
    Fixture() { fs::create_directories(root / "steamapps/common"); write(root / "steam.exe", "fixture"); }
    ~Fixture() { std::error_code ec; fs::remove_all(base, ec); }
    static void write(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s; }
    static std::string quote(const fs::path& p) { return p.generic_string(); }
    fs::path manifest(const fs::path& library, std::string id, std::string dir, std::string state = "4", std::string extra = "") {
        auto p = library / "steamapps" / ("appmanifest_" + id + ".acf");
        write(p, "\"AppState\" { \"appid\" \"" + id + "\" \"name\" \"Game \\\"Title\\\"\" \"installdir\" \"" + dir + "\" \"StateFlags\" \"" + state + "\" " + extra + " }");
        return p;
    }
};
}

TEST_CASE("Steam scan reads root and modern and legacy libraries deterministically") {
    Fixture f;
    auto second = f.base / "other library";
    auto third = f.base / "legacy";
    for (auto lib : {f.root, second, third}) fs::create_directories(lib / "steamapps/common/game");
    f.manifest(f.root, "10", "game");
    f.manifest(second, "20", "game");
    f.manifest(third, "30", "game");
    f.manifest(second, "10", "game");
    f.manifest(f.root, "228980", "game");
    Fixture::write(f.root / "steamapps/libraryfolders.vdf", "// libraries\n\"libraryfolders\" { \"1\" { \"path\" \"" + Fixture::quote(second) + "\" \"apps\" { \"20\" \"10\" } } \"2\" \"" + Fixture::quote(third) + "\" }");
    const auto result = SteamLibrary::scan(f.root);
    REQUIRE(result.games.size() == 3);
    CHECK(result.games[0].title == "Game \"Title\"");
    CHECK(result.games[0].appId == "10");
    CHECK(result.games[0].installPath == fs::canonical(f.root / "steamapps/common/game").string());
    CHECK(SteamLibrary::available(result.games[0]));
    const auto again = SteamLibrary::scan(f.root);
    REQUIRE(again.games.size() == result.games.size());
    for (std::size_t i = 0; i < again.games.size(); ++i) CHECK(again.games[i].manifestPath == result.games[i].manifestPath);
}

TEST_CASE("Steam scan isolates malformed manifests and rejects incomplete or unsafe installs") {
    Fixture f;
    fs::create_directories(f.root / "steamapps/common/game");
    fs::create_directories(f.root / "steamapps/outside");
    f.manifest(f.root, "1", "game");
    f.manifest(f.root, "2", "../outside");
    f.manifest(f.root, "3", "game", "1028");
    f.manifest(f.root, "4", "game", "4", "\"BytesToDownload\" \"100\" \"BytesDownloaded\" \"99\"");
    f.manifest(f.root, "5", "missing");
    Fixture::write(f.root / "steamapps/appmanifest_6.acf", "\"AppState\" { \"appid\"");
    auto mismatch = f.manifest(f.root, "7", "game");
    fs::rename(mismatch, f.root / "steamapps/appmanifest_8.acf");
    const auto result = SteamLibrary::scan(f.root);
    REQUIRE(result.games.size() == 1);
    CHECK(result.games[0].appId == "1");
    CHECK_FALSE(result.warnings.empty());
}

TEST_CASE("Steam availability rechecks files and manifest identity and state") {
    Fixture f;
    fs::create_directories(f.root / "steamapps/common/game");
    auto manifest = f.manifest(f.root, "1", "game");
    auto game = SteamLibrary::scan(f.root).games.at(0);
    SECTION("removed directory") { fs::remove(f.root / "steamapps/common/game"); }
    SECTION("removed executable") { fs::remove(f.root / "steam.exe"); }
    SECTION("removed manifest") { fs::remove(manifest); }
    SECTION("incomplete") { f.manifest(f.root, "1", "game", "1024"); }
    SECTION("changed identity") { game.appId = "2"; }
    SECTION("changed install directory") { fs::create_directories(f.root / "steamapps/common/other"); f.manifest(f.root, "1", "other"); }
    CHECK_FALSE(SteamLibrary::available(game));
}

TEST_CASE("Steam rejects symlink escapes") {
    Fixture f;
    fs::create_directories(f.base / "outside");
    std::error_code ec;
    fs::create_directory_symlink(f.base / "outside", f.root / "steamapps/common/escape", ec);
    if (ec) { SKIP("Directory symlink creation unavailable on this host"); }
    f.manifest(f.root, "1", "escape");
    CHECK(SteamLibrary::scan(f.root).games.empty());
}

TEST_CASE("Steam root validation uses stable errors") {
    Fixture f;
    try { SteamLibrary::scan(f.base / "missing"); FAIL("expected root validation"); }
    catch (const anime_vault::GameError& e) { CHECK(e.code == "invalid_steam_path"); }
    fs::remove(f.root / "steam.exe");
    CHECK_THROWS_AS(SteamLibrary::scan(f.root), anime_vault::GameError);
}

TEST_CASE("Steam parser bounds malformed input while preserving valid games") {
    Fixture f;
    fs::create_directories(f.root / "steamapps/common/game");
    f.manifest(f.root, "1", "game");
    SECTION("oversize file") { Fixture::write(f.root / "steamapps/appmanifest_2.acf", std::string(4 * 1024 * 1024 + 1, 'x')); }
    SECTION("deeply nested object") {
        std::string text;
        for (int i = 0; i < 40; ++i) text += "key { ";
        text += std::string(40, '}');
        Fixture::write(f.root / "steamapps/appmanifest_2.acf", text);
    }
    SECTION("duplicate identity field") { f.manifest(f.root, "2", "game", "4", "\"appid\" \"3\""); }
    SECTION("malformed library index") { Fixture::write(f.root / "steamapps/libraryfolders.vdf", "\"libraryfolders\" {"); }
    const auto result = SteamLibrary::scan(f.root);
    REQUIRE(result.games.size() == 1);
    CHECK_FALSE(result.warnings.empty());
}

TEST_CASE("Steam handles escaped library paths and UTF8 game directories") {
    Fixture f;
    const auto directory = std::string("\xe6\xb8\xb8\xe6\x88\x8f");
    const auto lib = f.base / "escaped library";
    const auto unicodePath = fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(directory.data()), directory.size()));
    fs::create_directories(lib / "steamapps/common" / unicodePath);
    f.manifest(lib, "11", directory);
    std::string escaped;
    const auto bytes = lib.u8string();
    for (const auto c : bytes) { if (c == '\\') escaped += '\\'; escaped += static_cast<char>(c); }
    Fixture::write(f.root / "steamapps/libraryfolders.vdf", "\xef\xbb\xbf\"LibraryFolders\" { \"0\" { \"path\" \"" + escaped + "\" } }");
    const auto result = SteamLibrary::scan(f.root);
    REQUIRE(result.games.size() == 1);
    CHECK(result.games[0].installPath.find(directory) != std::string::npos);
    CHECK(SteamLibrary::available(result.games[0]));
}
