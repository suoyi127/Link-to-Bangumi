#include "anime_vault/services/SteamImportService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>
#include <nlohmann/json.hpp>
using namespace anime_vault;
namespace fs = std::filesystem;
TEST_CASE("Steam metadata refresh preserves manual titles, other sources, and successful cached covers") {
    SqliteDatabase db(":memory:"); db.migrate(); SqliteGameRepository repo(db);
    GameResource game; game.path = "steam://rungameid/10"; game.title = "English"; game.steamAppId = "10";
    game.id = repo.importSteam(game);
    CachedCover cover{"image bytes", "image/png", "png"};
    repo.updateSteamMetadata(game.id, "10", "中文游戏", cover);
    REQUIRE(repo.list().front().title == "中文游戏");
    REQUIRE(repo.list().front().metadataSource == "steam");
    REQUIRE(repo.cover(game.id)->bytes == cover.bytes);
    repo.updateSteamMetadata(game.id, "10", "", {});
    REQUIRE(repo.list().front().title == "中文游戏");
    REQUIRE(repo.cover(game.id)->bytes == cover.bytes);
    repo.updateMetadata(game.id, "手动名称", "", "", game.path);
    repo.updateSteamMetadata(game.id, "10", "自动名称", {});
    REQUIRE(repo.list().front().title == "手动名称");
    repo.bind(game.id, 123, "Bangumi", "", "", "PC");
    repo.setCover(game.id, 123, "Bangumi cover", "image/jpeg");
    repo.updateSteamMetadata(game.id, "10", "覆盖尝试", cover);
    REQUIRE(repo.list().front().metadataSource == "bangumi");
    REQUIRE(repo.cover(game.id)->bytes == "Bangumi cover");
    repo.remove(game.id);
    repo.updateSteamMetadata(game.id, "10", "中文游戏", cover);
    REQUIRE(repo.list().empty());
}
TEST_CASE("Steam imports public Chinese metadata and covers even without statistics credentials") {
    const auto root = fs::temp_directory_path() / ("av-steam-metadata-" + std::to_string(std::random_device{}()));
    fs::create_directories(root / "steamapps/common/Game");
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{root};
    std::ofstream(root / "steam.exe") << "MZtest";
    std::ofstream(root / "steamapps/appmanifest_10.acf") << R"("AppState" { "appid" "10" "name" "English" "StateFlags" "4" "installdir" "Game" })";
    SqliteDatabase db(root / "games.sqlite"); db.migrate(); SqliteGameRepository repo(db);
    SteamClient stats([](auto, auto) { FAIL("No statistics credentials"); });
    const std::string png = "\x89PNG\r\n\x1a\nfixture";
    SteamMetadataClient metadata([&](std::string host, std::string, auto done) {
        if (host == "store.steampowered.com") done(200, nlohmann::json{{"10", {{"success", true}, {"data", {{"name", "中文游戏"}, {"header_image", "https://shared.akamai.steamstatic.com/steam/apps/10/header.jpg"}}}}}}.dump());
        else done(200, png);
    });
    auto service = std::make_shared<SteamImportService>(repo, stats, &metadata);
    const auto path = root.u8string(); SteamConfig config{std::string(path.begin(), path.end()), "", ""};
    int completions = 0;
    service->importInstalled(config, [&](auto value) { ++completions; REQUIRE(value.errorCode.empty()); });
    REQUIRE(completions == 1); REQUIRE_FALSE(service->busy());
    REQUIRE(repo.list().front().title == "中文游戏");
    REQUIRE(repo.cover(repo.list().front().id)->bytes == png);
    service->importInstalled(config, [&](auto value) { REQUIRE(value.imported == 0); REQUIRE(value.updated == 1); });
    REQUIRE(repo.list().front().title == "中文游戏");
    std::function<void(int, std::string)> pending;
    SteamMetadataClient delayed([&](std::string, std::string, auto done) { pending = std::move(done); });
    auto waiting = std::make_shared<SteamImportService>(repo, stats, &delayed);
    waiting->importInstalled(config, [&](auto) { ++completions; });
    REQUIRE(waiting->busy()); REQUIRE(pending);
    repo.remove(repo.list().front().id);
    pending(500, "error");
    REQUIRE(repo.list().empty()); REQUIRE_FALSE(waiting->busy()); REQUIRE(completions == 2);
    waiting->importInstalled(config, [&](auto) { FAIL("No completion after shutdown"); });
    waiting->stop();
    pending(500, "error");
}
TEST_CASE("Steam imports locally without credentials and refreshes rather than duplicates") {
    const auto root = fs::temp_directory_path() / ("av-steam-import-" + std::to_string(std::random_device{}()));
    fs::create_directories(root / "steamapps/common/Dota");
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code error; fs::remove_all(root, error); } } cleanup{root};
    std::ofstream(root / "steam.exe") << "MZtest";
    std::ofstream(root / "steamapps/appmanifest_570.acf") << R"("AppState" { "appid" "570" "name" "Dota" "StateFlags" "4" "installdir" "Dota" })";
    SqliteDatabase db(root / "games.sqlite"); db.migrate(); SqliteGameRepository repo(db);
    SteamClient client([](auto, auto) { FAIL("Offline import must not make network requests"); });
    auto service = std::make_shared<SteamImportService>(repo, client);
    const auto name = root.u8string();
    SteamConfig config{std::string(name.begin(), name.end()), "", ""};
    bool hadLocal = false;
    SECTION("new game") {}
    SECTION("already registered by executable") {
        const auto exe = root / "steamapps/common/Dota/game.exe";
        std::ofstream(exe) << "MZ";
        const auto path = exe.u8string();
        repo.importExecutable({path.begin(), path.end()}, "Dota");
        hadLocal = true;
    }
    std::optional<SteamImportResult> result;
    service->importInstalled(config, [&](auto value) { result = value; });
    REQUIRE(result.has_value()); REQUIRE(result->errorCode.empty()); REQUIRE(result->imported == (hadLocal ? 0 : 1)); REQUIRE(result->updated == (hadLocal ? 1 : 0));
    REQUIRE(repo.list().front().path == "steam://rungameid/570");
    REQUIRE_FALSE(repo.list().front().playtimeMinutes.has_value());
    REQUIRE_FALSE(repo.list().front().steamStatsError.empty());
    REQUIRE_FALSE(service->busy());
    service->importInstalled(config, [&](auto value) { result = value; });
    REQUIRE(result->imported == 0); REQUIRE(result->updated == 1);
    REQUIRE(repo.list().size() == 1);
    config.steamId = "76561198000000000"; config.apiKey = std::string(32, 'a');
    SteamClient online([](std::string path, auto done) {
        if (path.find("GetOwnedGames") != std::string::npos)
            done(200, R"({"response":{"games":[{"appid":570,"playtime_forever":135}]}})");
        else done(200, R"({"playerstats":{"success":true,"achievements":[{"achieved":1},{"achieved":0}]}})");
    });
    auto syncing = std::make_shared<SteamImportService>(repo, online);
    syncing->importInstalled(config, [&](auto value) { result = value; });
    REQUIRE(result->warnings.empty());
    REQUIRE(repo.list().front().playtimeMinutes == 135);
    REQUIRE(repo.list().front().achievementsUnlocked == 1);
    REQUIRE(repo.list().front().achievementsTotal == 2);
    std::function<void(int, std::string)> pending;
    SteamClient delayed([&](std::string, auto done) { pending = std::move(done); });
    auto waiting = std::make_shared<SteamImportService>(repo, delayed);
    waiting->importInstalled(config, [&](auto value) { result = value; });
    REQUIRE(waiting->busy());
    waiting->importInstalled(config, [&](auto value) { REQUIRE(value.errorCode == "steam_import_in_progress"); });
    REQUIRE(pending);
    pending(0, "");
    REQUIRE_FALSE(waiting->busy());
    REQUIRE(result->errorCode.empty());
    REQUIRE_FALSE(result->warnings.empty());
    REQUIRE(repo.list().front().playtimeMinutes == 135);
    REQUIRE(repo.list().front().achievementsUnlocked == 1);
    waiting->importInstalled(config, [&](auto value) { result = value; });
    repo.remove(repo.list().front().id);
    pending(0, "");
    REQUIRE(repo.list().empty());
    waiting->importInstalled(config, [&](auto) { FAIL("Shutdown must cancel the pending completion"); });
    waiting->stop();
    repo.remove(repo.list().front().id);
    pending(0, "");
    REQUIRE(repo.list().empty());
}
