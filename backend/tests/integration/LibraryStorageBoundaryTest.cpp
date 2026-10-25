#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteNovelRepository.hpp"
#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include "anime_vault/infrastructure/NativeLibraryFileAccess.hpp"
#include "anime_vault/services/NovelBangumiService.hpp"
#include "anime_vault/services/GameBangumiService.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <future>
using namespace anime_vault;
namespace {
void exec(SqliteDatabase& db, const char* sql) { REQUIRE(sqlite3_exec(db.handle(), sql, nullptr, nullptr, nullptr) == SQLITE_OK); }
struct DeferredTransport : BangumiTransport {
    Completion pending;
    void search(std::string, Completion) override { FAIL("unexpected search"); }
    void searchAliases(std::string, Completion) override { FAIL("unexpected alias search"); }
    void subject(std::int64_t, Completion completion) override { pending = std::move(completion); }
};
struct NoImages : CoverImageFetcher {
    void fetch(std::string, Completion) override { FAIL("unexpected image fetch"); }
};
}
TEST_CASE("Library prepare failures keep family errors and complete scrape callbacks") {
    SqliteDatabase database(":memory:");
    SqliteNovelRepository novels(database); SqliteGameRepository games(database); NativeLibraryFileAccess files;
    NovelService novelService(novels, files); GameService gameService(games, files);
    REQUIRE_THROWS_AS(novels.listWorksWithFiles(), NovelError);
    REQUIRE_THROWS_AS(games.list(), GameError);
    DeferredTransport transport; NoImages images;
    auto novelScraper = std::make_shared<NovelBangumiService>(novelService, transport, images);
    auto gameScraper = std::make_shared<GameBangumiService>(gameService, transport, images);
    int completions = 0;
    novelScraper->scrape(1, false, {}, [&](NovelScrapeResult result) { ++completions; REQUIRE(result.errorCode == "novel_storage_error"); });
    gameScraper->scrape(1, {}, [&](GameScrapeResult result) { ++completions; REQUIRE(result.errorCode == "game_storage_error"); });
    REQUIRE(completions == 2);
}
TEST_CASE("Deferred library bindings report SQL step failures with their original family code") {
    SqliteDatabase database(":memory:"); database.migrate();
    SqliteNovelRepository novels(database); SqliteGameRepository games(database); NativeLibraryFileAccess files;
    NovelService novelService(novels, files); GameService gameService(games, files);
    exec(database, "INSERT INTO novel_work(id,import_key,title) VALUES(1,'story','Story')");
    const auto gameId = games.importExecutable("C:/unavailable-story.exe", "Story");
    DeferredTransport transport; NoImages images;
    auto novelScraper = std::make_shared<NovelBangumiService>(novelService, transport, images);
    auto gameScraper = std::make_shared<GameBangumiService>(gameService, transport, images);
    int completions = 0;
    novelScraper->scrape(1, false, 870, [&](NovelScrapeResult result) { ++completions; REQUIRE(result.errorCode == "novel_storage_error"); });
    REQUIRE(completions == 0); REQUIRE(static_cast<bool>(transport.pending));
    exec(database, "CREATE TRIGGER refuse_novel BEFORE UPDATE ON novel_work BEGIN SELECT RAISE(ABORT,'refused'); END");
    transport.pending(BangumiTransport::Response{200, R"({"id":870,"type":1,"name":"Story","platform":"小说"})"}, "");
    REQUIRE(completions == 1);
    gameScraper->scrape(gameId, 871, [&](GameScrapeResult result) { ++completions; REQUIRE(result.errorCode == "game_storage_error"); });
    REQUIRE(completions == 1);
    exec(database, "CREATE TRIGGER refuse_game BEFORE UPDATE ON game_resource BEGIN SELECT RAISE(ABORT,'refused'); END");
    transport.pending(BangumiTransport::Response{200, R"({"id":871,"type":4,"name":"Story","platform":"PC"})"}, "");
    REQUIRE(completions == 2);
}
TEST_CASE("Novel reader malformed JSON keeps its parser exception") {
    SqliteDatabase database(":memory:"); database.migrate(); SqliteNovelRepository novels(database);
    exec(database, "INSERT INTO setting(key,value_json) VALUES('novelReader','{')");
    REQUIRE_THROWS_AS(novels.reader(), nlohmann::json::parse_error);
}
TEST_CASE("Library repository scopes share the original recursive database mutex") {
    SqliteDatabase database(":memory:"); database.migrate();
    SqliteNovelRepository novels(database); SqliteGameRepository games(database);
    {
        auto outer = novels.lock(); auto nested = games.lock();
        REQUIRE(novels.listSources().empty()); REQUIRE(games.list().empty());
        auto otherThread = std::async(std::launch::async, [&] {
            const bool acquired = database.mutex().try_lock();
            if (acquired) database.mutex().unlock();
            return acquired;
        });
        REQUIRE_FALSE(otherThread.get());
    }
    auto released = std::async(std::launch::async, [&] {
        const bool acquired = database.mutex().try_lock();
        if (acquired) database.mutex().unlock();
        return acquired;
    });
    REQUIRE(released.get());
}
TEST_CASE("Failed novel snapshot rolls back source registration and partial records") {
    SqliteDatabase database(":memory:"); database.migrate(); SqliteNovelRepository novels(database);
    exec(database, "CREATE TRIGGER refuse_file BEFORE INSERT ON novel_file BEGIN SELECT RAISE(ABORT,'refused'); END");
    REQUIRE_THROWS_WITH(novels.importSnapshot("C:/books", true, {{"C:/books/Story.txt", "Story", "Story", "story"}}), "novel_storage_error");
    REQUIRE(novels.listSources().empty()); REQUIRE(novels.listWorksWithFiles().empty());
    REQUIRE(sqlite3_get_autocommit(database.handle()) != 0);
}
