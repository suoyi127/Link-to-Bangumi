#include "anime_vault/infrastructure/NativeLibraryFileAccess.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include "anime_vault/services/GameService.hpp"
#include "anime_vault/services/SteamPoster.hpp"
#include "anime_vault/services/GameBangumiService.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>
using namespace anime_vault;
namespace fs = std::filesystem;
struct GameFixture {
    fs::path root = fs::temp_directory_path() / ("av-game-" + std::to_string(std::random_device{}()));
    GameFixture() { fs::create_directories(root); std::ofstream(root / "Story.exe") << "MZtest"; }
    ~GameFixture() { std::error_code error; fs::remove_all(root, error); }
};
TEST_CASE("Steam poster selection uses the AppID cache and preserves bound artwork") {
    GameFixture fixture;
    const auto dir = fixture.root / "appcache/librarycache/730";
    const auto nested = dir / std::string(40,'b');
    fs::create_directories(nested);
    const std::string png = "\x89PNG\r\n\x1a\n";
    std::ofstream(dir / "library_600x900.png",std::ios::binary) << png << "generic";
    std::ofstream(nested / "library_capsule_schinese.png",std::ios::binary) << png << "local";
    GameResource game; game.steamAppId = "730"; game.metadataSource = "steam";
    const auto executable=(fixture.root/"steam.exe").u8string(); game.steamExecutable={executable.begin(),executable.end()};
    auto selected = readSteamPoster(game);
    REQUIRE(selected); CHECK(selected->bytes == png + "local");
    std::ofstream(nested / "library_capsule_schinese.png",std::ios::binary) << "invalid";
    selected = readSteamPoster(game); REQUIRE(selected); CHECK(selected->bytes == png + "generic");
    game.metadataSource = "bangumi"; CHECK_FALSE(readSteamPoster(game)); game.metadataSource = "steam";
    game.vndbId = "v10"; CHECK_FALSE(readSteamPoster(game)); game.vndbId.clear();
    game.steamAppId = "../730"; CHECK_FALSE(readSteamPoster(game));
    game.steamAppId = "10"; CHECK_FALSE(readSteamPoster(game));
}
TEST_CASE("Steam and manual entry points share a game identity and preserve its bound artwork") {
    GameFixture f;
    const auto install = f.root / "Game";
    fs::create_directories(install / "bin");
    auto exe = install / "bin/game.exe";
    auto adjacent = f.root / "Game Two/game.exe";
    fs::create_directories(adjacent.parent_path());
    std::ofstream(exe) << "game"; std::ofstream(adjacent) << "other";
    auto utf = [](const fs::path& p) { const auto v=p.u8string(); return std::string(v.begin(),v.end()); };
    SqliteDatabase db(":memory:"); db.migrate(); SqliteGameRepository repo(db);
    const auto local = repo.importExecutable(utf(exe),"Game");
    repo.bind(local,42,"Bound title","Developer","Summary","PC");
    repo.setCover(local,42,"manual cover","image/png");
    repo.updateMetadata(local, "My title", "Edited developer", "My summary", utf(exe));
    const auto separate = repo.importExecutable(utf(adjacent),"Game");
    GameResource steam; steam.path="steam://rungameid/10"; steam.steamAppId="10"; steam.steamInstallPath=utf(install);
    steam.title="Store title"; steam.playtimeMinutes=15;
    auto id=repo.importSteam(steam);
    REQUIRE(repo.canonicalId(local)==id);
    REQUIRE(repo.importExecutable(utf(exe),"Game")==id);
    REQUIRE(repo.importSteam(steam)==id);
    const auto shortcut = f.root / "Game copy.exe";
    fs::create_hard_link(exe, shortcut);
    bool reused = false;
    CHECK(repo.importExecutable(utf(shortcut), "Game", &reused) == id);
    CHECK(reused);
    REQUIRE(repo.canonicalId(separate)==separate);
    REQUIRE(repo.list().size()==2);
    auto rows=repo.list();
    auto merged=std::find_if(rows.begin(),rows.end(),[&](const auto& g){return g.id==id;});
    REQUIRE(merged!=rows.end()); CHECK(merged->subjectId==42); CHECK(merged->title=="My title");
    CHECK(merged->manualMetadata); CHECK(merged->developer=="Edited developer");
    CHECK(merged->metadataSource=="bangumi"); CHECK(merged->playtimeMinutes==15);
    REQUIRE(repo.cover(local)); CHECK(repo.cover(local)->bytes=="manual cover");
    NativeLibraryFileAccess files; GameService service(repo, files);
    CHECK(service.get(local).id == id);
    CHECK(service.metadataForCover(local).subjectId == 42);
    service.edit(local, "Edited title", "Edited developer", "", steam.path);
    CHECK(service.get(id).title == "Edited title");
    repo.reconcileImportPaths(); REQUIRE(repo.list().size()==2);
    service.removeUnavailable(local);
    CHECK(repo.list().size() == 1);
    CHECK(fs::exists(exe));
}
TEST_CASE("Steam-first imports reuse a nested executable without merging unrelated equal filenames") {
    GameFixture f;
    const auto root=f.root / "Game";
    fs::create_directories(root/"bin"); std::ofstream(root/"bin/story.exe") << "MZgame";
    SqliteDatabase db(":memory:"); db.migrate(); SqliteGameRepository repo(db);
    GameResource s; s.path="steam://rungameid/7"; s.steamAppId="7"; s.title="Story"; const auto uroot = root.u8string();
    s.steamInstallPath.assign(uroot.begin(), uroot.end());
    auto canonical=repo.importSteam(s);
    NativeLibraryFileAccess files; GameService service(repo, files);
    REQUIRE(service.importExecutable(root/"bin/story.exe").id == canonical);
    const auto secondPath = f.root / "Another.exe";
    fs::create_hard_link(root/"bin/story.exe", secondPath);
    bool reused = false;
    CHECK(service.importExecutable(secondPath, &reused).id == canonical);
    CHECK(reused);
    auto outside=f.root/"story.exe"; std::ofstream(outside) << "MZother";
    REQUIRE(service.importExecutable(outside).id!=canonical);
    REQUIRE(repo.list().size()==2);
}
TEST_CASE("Conflicting manual edits or bindings keep both game records") {
    GameFixture f;
    const auto exe = (f.root / "Story.exe").u8string();
    const auto root = f.root.u8string();
    SqliteDatabase db(":memory:"); db.migrate(); SqliteGameRepository repo(db);
    const auto local = repo.importExecutable({exe.begin(), exe.end()}, "Local");
    GameResource steam;
    steam.path = "steam://rungameid/7"; steam.steamAppId = "7"; steam.title = "Steam";
    const auto id = repo.importSteam(steam);
    SECTION("bindings") {
        repo.bind(local, 42, "Local", "", "", "");
        repo.bind(id, 52, "Steam", "", "", "");
    }
    SECTION("manual edits") {
        repo.updateMetadata(local, "My game", "", "", {exe.begin(), exe.end()});
        repo.updateMetadata(id, "My Steam game", "", "", steam.path);
    }
    steam.steamInstallPath.assign(root.begin(), root.end());
    repo.importSteam(steam);
    repo.reconcileImportPaths();
    REQUIRE(repo.list().size() == 2);
    CHECK(repo.canonicalId(local) == local);
}
TEST_CASE("A Steam scan leaves a manual game separate when two AppIDs share its directory") {
    GameFixture f;
    const auto path = (f.root / "Story.exe").u8string();
    const auto install = f.root.u8string();
    SqliteDatabase db(":memory:"); db.migrate(); SqliteGameRepository repo(db);
    const auto local = repo.importExecutable({path.begin(), path.end()}, "Story");
    std::vector<GameResource> batch;
    for (const auto* app : {"7", "8"}) {
        GameResource game;
        game.steamAppId = app;
        game.path = "steam://rungameid/" + game.steamAppId;
        game.title = "Shared directory";
        game.steamInstallPath.assign(install.begin(), install.end());
        batch.push_back(game);
    }
    CHECK(repo.importSteamBatch(batch) == 0);
    CHECK(repo.canonicalId(local) == local);
    CHECK(repo.list().size() == 3);
}
struct GameLauncher : ProcessLauncher {
    fs::path program, directory;
    bool launch(const fs::path& exe, const std::vector<std::string>& args) override { program = exe; return args.empty(); }
    bool launchInDirectory(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd) override { directory = cwd; return launch(exe, args); }
};
TEST_CASE("Game paths accept paired copied quotes and surrounding whitespace") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); SqliteGameRepository gamesRepository(db); NativeLibraryFileAccess gamesFiles; GameService games(gamesRepository, gamesFiles);
    const auto path = (fixture.root / "Story.exe").u8string();
    const auto plain = games.importExecutable(fixture.root / "Story.exe");
    REQUIRE(games.importExecutable(fs::path(u8" \"" + path + u8"\" \r\n")).id == plain.id);
    REQUIRE(games.importExecutable(fs::path(u8" “" + path + u8"” ")).id == plain.id);
    games.edit(plain.id, "游戏", "", "", "“" + plain.path + "”");
    REQUIRE(games.get(plain.id).path == plain.path);
    REQUIRE_THROWS_AS(games.importExecutable(fs::path(u8"\"" + path)), GameError);
}
TEST_CASE("Game import is idempotent and preserves manual metadata") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); SqliteGameRepository gamesRepository(db); NativeLibraryFileAccess gamesFiles; GameService games(gamesRepository, gamesFiles);
    auto game = games.importExecutable(fixture.root / "Story.exe");
    games.edit(game.id, "中文游戏", "开发商", "简介", game.path);
    REQUIRE(games.importExecutable(fixture.root / "Story.exe").id == game.id);
    REQUIRE(games.list().size() == 1);
    REQUIRE(games.get(game.id).title == "中文游戏");
    REQUIRE_THROWS_AS(games.removeUnavailable(game.id), GameError);
    GameLauncher launcher; games.launch(game.id, launcher);
    REQUIRE(fs::equivalent(launcher.program, fixture.root / "Story.exe"));
    REQUIRE(fs::equivalent(launcher.directory, fixture.root));
    fs::rename(fixture.root / "Story.exe", fixture.root / "saved.exe");
    REQUIRE(games.get(game.id).missing);
    REQUIRE_THROWS_AS(games.launch(game.id, launcher), GameError);
    games.removeUnavailable(game.id);
    REQUIRE(games.list().empty());
    REQUIRE(fs::exists(fixture.root / "saved.exe"));
}

TEST_CASE("Steam import preserves manual metadata and failed refresh retains same-account statistics") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); SqliteGameRepository repo(db);
    GameResource game; game.steamAppId = "570"; game.path = "steam://rungameid/570"; game.title = "Dota";
    game.steamId = "76561198000000000"; game.playtimeMinutes = 120;
    game.achievementsUnlocked = 0; game.achievementsTotal = 10;
    const auto id = repo.importSteam(game);
    repo.updateMetadata(id, "Custom title", "Developer", "Summary", game.path);
    game.playtimeMinutes.reset(); game.achievementsUnlocked.reset(); game.achievementsTotal.reset();
    game.steamStatsError = "steam_stats_unavailable";
    REQUIRE(repo.importSteam(game) == id);
    auto saved = repo.list().front();
    REQUIRE(saved.title == "Custom title");
    REQUIRE(saved.playtimeMinutes == 120);
    REQUIRE(saved.achievementsUnlocked == 0);
    REQUIRE(saved.steamStatsError == "steam_stats_unavailable");
    game.steamId = "76561198000000001";
    repo.importSteam(game);
    saved = repo.list().front();
    REQUIRE_FALSE(saved.playtimeMinutes.has_value());
    REQUIRE_FALSE(saved.achievementsUnlocked.has_value());
    REQUIRE(repo.list().size() == 1);
}

TEST_CASE("Steam game launches through Steam and cannot be redirected to arbitrary URI") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); SqliteGameRepository repo(db);
    fs::create_directories(fixture.root / "steamapps/common/Dota");
    std::ofstream(fixture.root / "steam.exe") << "MZtest";
    std::ofstream(fixture.root / "steamapps/appmanifest_570.acf") << R"("AppState" { "appid" "570" "name" "Dota" "StateFlags" "4" "installdir" "Dota" })";
    const auto utf8 = [](const fs::path& p) { const auto s = p.u8string(); return std::string(s.begin(), s.end()); };
    GameResource entry; entry.steamAppId = "570"; entry.title = "Dota"; entry.path = "steam://rungameid/570";
    entry.steamInstallPath = utf8(fixture.root / "steamapps/common/Dota");
    entry.steamManifestPath = utf8(fixture.root / "steamapps/appmanifest_570.acf");
    entry.steamExecutable = utf8(fixture.root / "steam.exe");
    const auto id = repo.importSteam(entry);
    NativeLibraryFileAccess files; GameService games(repo, files);
    REQUIRE_FALSE(games.get(id).missing);
    struct Launcher : ProcessLauncher {
        fs::path program; std::vector<std::string> arguments;
        bool launch(const fs::path& p, const std::vector<std::string>& a) override { program = p; arguments = a; return true; }
    } launcher;
    games.launch(id, launcher);
    REQUIRE(launcher.program == fixture.root / "steam.exe");
    REQUIRE(launcher.arguments == std::vector<std::string>{"steam://rungameid/570"});
    REQUIRE_THROWS_AS(games.edit(id, "Title", "", "", "steam://install/1"), GameError);
    fs::rename(fixture.root / "steamapps/appmanifest_570.acf", fixture.root / "manifest.backup");
    REQUIRE(games.get(id).missing);
    REQUIRE_THROWS_AS(games.launch(id, launcher), GameError);
}
struct GameTransport : BangumiTransport {
    std::string game = R"({"id":870,"type":4,"platform":"PC","name":"Story","name_cn":"故事游戏","images":{"large":"https://lain.bgm.tv/pic/cover/l/aa/bb/870_test.jpg"},"infobox":[{"key":"开发","value":"Studio"},{"key":"别名","value":[{"v":"Alias"}]}]})";
    void search(std::string, Completion completion) override { completion({}, "wrong_route"); }
    void searchAliases(std::string, Completion completion) override { completion({}, "wrong_route"); }
    void searchGames(std::string, Completion completion) override { completion(Response{200, "{\"data\":[" + game + "]}"}, ""); }
    void subject(std::int64_t, Completion completion) override { completion(Response{200, game}, ""); }
};
struct GameCovers : CoverImageFetcher {
    void fetch(std::string, Completion completion) override { completion(std::string("\xff\xd8\xff\xd9", 4), ""); }
};
struct VndbStub : VndbTransport {
    int searches{};
    bool more{}, deferCover{};
    std::vector<CoverImageFetcher::Completion> pending;
    std::string game = R"({"id":"v4","title":"Story","titles":[{"lang":"zh-Hans","title":"VN中文游戏","latin":null}],"aliases":["Alias"],"description":"[b]简介[/b]","developers":[{"name":"VN Studio"}],"platforms":["win"],"image":{"url":"https://t.vndb.org/cv/95/75895.jpg"}})";
    void search(std::string, VndbTransport::Completion completion) override { ++searches; completion(Response{200, "{\"results\":[" + game + "],\"more\":" + (more ? "true" : "false") + "}"}, ""); }
    void subject(std::string, VndbTransport::Completion completion) override { completion(Response{200, "{\"results\":[" + game + "],\"more\":false}"}, ""); }
    void fetch(std::string, CoverImageFetcher::Completion completion) override { if (deferCover) pending.push_back(std::move(completion)); else completion(std::string("\xff\xd8\xff\xd9", 4), ""); }
};
TEST_CASE("VNDB is used only when initial Bangumi matching fails") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); SqliteGameRepository gamesRepository(db); NativeLibraryFileAccess gamesFiles; GameService games(gamesRepository, gamesFiles);
    const auto game = games.importExecutable(fixture.root / "Story.exe");
    GameTransport transport; GameCovers covers; VndbStub vndb;
    auto scraper = std::make_shared<GameBangumiService>(games, transport, covers, &vndb);
    GameScrapeResult result;
    scraper->scrape(game.id, {}, [&](auto value) { result = value; });
    REQUIRE(result.source == "bangumi"); REQUIRE(vndb.searches == 0);
    const auto subject = GameBangumiService::parseVndbSubject(vndb.game);
    REQUIRE(subject.nameCn == "VN中文游戏"); REQUIRE(subject.summary == "简介");
    REQUIRE(GameBangumiService::uniqueVndbMatch("Alias", {subject}) == "v4");
    REQUIRE(VndbTransport::allowedCoverPath(subject.coverUrl));
    REQUIRE_FALSE(VndbTransport::allowedCoverPath("https://example.com/cv/95/75895.jpg"));
    REQUIRE_FALSE(VndbTransport::allowedCoverPath("https://t.vndb.org/cv/95/../75895.jpg"));
    std::ofstream(fixture.root / fs::path(u8"VN中文游戏.exe")) << "MZtest";
    const auto other = games.importExecutable(fixture.root / fs::path(u8"VN中文游戏.exe"));
    scraper->scrape(other.id, {}, [&](auto value) { result = value; });
    REQUIRE(result.bound); REQUIRE(result.coverUpdated); REQUIRE(result.source == "vndb");
    REQUIRE(games.get(other.id).vndbId == "v4"); REQUIRE_FALSE(games.get(other.id).subjectId);
    REQUIRE(games.get(other.id).title == "VN中文游戏"); REQUIRE(games.get(other.id).metadataSource == "vndb");
    games.edit(other.id, "人工中文", "人工开发", "人工简介", other.path);
    scraper->scrape(other.id, {}, [&](auto value) { result = value; });
    REQUIRE(games.get(other.id).title == "人工中文"); REQUIRE(vndb.searches == 1);
}
TEST_CASE("VNDB rejects incomplete search uniqueness and late covers from another source") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); SqliteGameRepository gamesRepository(db); NativeLibraryFileAccess gamesFiles; GameService games(gamesRepository, gamesFiles);
    const auto game = games.importExecutable(fixture.root / "Story.exe");
    GameTransport transport; GameCovers covers; VndbStub vndb; vndb.more = true;
    auto scraper = std::make_shared<GameBangumiService>(games, transport, covers, &vndb);
    GameScrapeResult result; scraper->scrapeVndb(game.id, {}, [&](auto value) { result = value; });
    REQUIRE_FALSE(result.bound); REQUIRE(result.errorCode == "vndb_match_requires_confirmation");
    vndb.deferCover = true;
    scraper->scrapeVndb(game.id, "v4", [&](auto value) { result = value; });
    REQUIRE(games.get(game.id).metadataSource == "vndb");
    scraper->scrape(game.id, 870, [&](auto value) { result = value; });
    REQUIRE(games.get(game.id).metadataSource == "bangumi");
    vndb.pending.front()(std::string("\xff\xd8\xff\xd9", 4), "");
    REQUIRE_FALSE(result.coverUpdated);
    REQUIRE(games.get(game.id).subjectId == 870); REQUIRE(games.get(game.id).vndbId == "v4");
    REQUIRE_FALSE(games.setVndbCover(game.id, "v4", "bad", "image/jpeg"));
}
TEST_CASE("Game scraping filters subject types and preserves edited titles") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); SqliteGameRepository gamesRepository(db); NativeLibraryFileAccess gamesFiles; GameService games(gamesRepository, gamesFiles);
    auto game = games.importExecutable(fixture.root / "Story.exe");
    GameTransport transport; GameCovers covers;
    auto scraper = std::make_shared<GameBangumiService>(games, transport, covers);
    GameScrapeResult result; scraper->scrape(game.id, {}, [&](auto value) { result = value; });
    REQUIRE(result.bound); REQUIRE(result.coverUpdated);
    REQUIRE(games.get(game.id).title == "故事游戏"); REQUIRE(games.get(game.id).developer == "Studio");
    REQUIRE(games.cover(game.id));
    const auto subject = GameBangumiService::parseSubject(transport.game);
    REQUIRE(GameBangumiService::uniqueMatch("Alias", {subject}) == 870);
    auto other = subject; other.id = 871;
    REQUIRE_FALSE(GameBangumiService::uniqueMatch("Alias", {subject, other}));
    REQUIRE_THROWS_AS(GameBangumiService::parseSubject(R"({"id":2,"type":2,"name":"Story"})"), GameError);
    games.edit(game.id, "人工标题", "人工开发商", "人工简介", game.path);
    scraper->scrape(game.id, {}, [&](auto value) { result = value; });
    REQUIRE(games.get(game.id).title == "人工标题");
}
