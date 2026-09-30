#include "anime_vault/services/GameService.hpp"
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
struct GameLauncher : ProcessLauncher {
    fs::path program, directory;
    bool launch(const fs::path& exe, const std::vector<std::string>& args) override { program = exe; return args.empty(); }
    bool launchInDirectory(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd) override { directory = cwd; return launch(exe, args); }
};
TEST_CASE("Game paths accept paired copied quotes and surrounding whitespace") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); GameService games(db);
    const auto path = (fixture.root / "Story.exe").u8string();
    const auto plain = games.importExecutable(fixture.root / "Story.exe");
    REQUIRE(games.importExecutable(fs::path(u8" \"" + path + u8"\" \r\n")).id == plain.id);
    REQUIRE(games.importExecutable(fs::path(u8" “" + path + u8"” ")).id == plain.id);
    games.edit(plain.id, "游戏", "", "", "“" + plain.path + "”");
    REQUIRE(games.get(plain.id).path == plain.path);
    REQUIRE_THROWS_AS(games.importExecutable(fs::path(u8"\"" + path)), GameError);
}
TEST_CASE("Game import is idempotent and preserves manual metadata") {
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); GameService games(db);
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
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); GameService games(db);
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
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); GameService games(db);
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
    GameFixture fixture; SqliteDatabase db(fixture.root / "db.sqlite"); db.migrate(); GameService games(db);
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
