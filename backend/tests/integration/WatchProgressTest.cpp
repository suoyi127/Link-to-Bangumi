#include "anime_vault/infrastructure/database/WatchProgressStore.hpp"
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

using namespace anime_vault;

TEST_CASE("watch progress keeps version-specific resume, shares completion and ignores stale sessions") {
    SqliteDatabase db(":memory:");
    db.migrate();
    REQUIRE(
        sqlite3_exec(db.handle(),
                     "INSERT INTO anime(id,display_title) VALUES(1,'Series');"
                     "INSERT INTO "
                     "media_file(id,anime_id,source_path,filename,episode_number,episode_type,size_bytes,status) VALUES"
                     "(1,1,'a','a','01','normal',1,'inbox'),(2,1,'b','b','1','normal',1,'inbox'),"
                     "(3,1,'c','c','02','normal',1,'inbox'),(4,1,'d','d','1','sp',1,'inbox');",
                     nullptr, nullptr, nullptr) == SQLITE_OK);
    WatchProgressStore store(db);
    REQUIRE(store.continuing().empty());
    auto a = store.begin(1, false);
    store.sample(1, a.id, 70, 100, {{50, 70}});
    REQUIRE(store.continuing().empty()); // seek contributes no extra coverage
    store.sample(1, a.id, 85, 100, {{65, 85}});
    REQUIRE(store.continuing().front().mediaId == 1);
    auto b = store.begin(2, false);
    REQUIRE(b.position == 0);
    store.sample(1, a.id, 100, 100, {{0, 100}});
    REQUIRE_FALSE(store.list(1).front().watched);
    REQUIRE(store.begin(1, false).position == 85);
    auto restart = store.begin(1, true);
    REQUIRE(restart.position == 0);
    store.sample(1, restart.id, 50, 100, {{0, 50}});
    store.sample(1, restart.id, 98, 100, {{49, 98}});
    auto rows = store.list(1);
    REQUIRE(rows[0].watched);
    REQUIRE(rows[1].watched);
    REQUIRE_FALSE(rows[3].watched);
    REQUIRE(store.continuing().front().mediaId == 3);
    REQUIRE(store.continuing().front().nextEpisode);
    auto repeat = store.begin(2, false);
    store.sample(2, repeat.id, 100, 100, {{0, 100}});
    sqlite3_stmt *count = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT count(*) FROM watch_progress WHERE watched=1", -1, &count,
                               nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(count) == SQLITE_ROW);
    CHECK(sqlite3_column_int(count, 0) == 1);
    sqlite3_finalize(count);
    store.mark(2, false);
    REQUIRE_FALSE(store.list(1).front().watched);
    REQUIRE(store.continuing().empty());
}

TEST_CASE("existing file progress follows an anime merge, with no preexisting watch history inferred") {
    SqliteDatabase db(":memory:");
    db.migrate();
    REQUIRE(sqlite3_exec(db.handle(), R"sql(
      INSERT INTO anime(id,display_title) VALUES(1,'Older'),(2,'Bound');
      INSERT INTO media_file(id,anime_id,source_path,filename,episode_number,size_bytes,status)
      VALUES(11,1,'a','a','4',1,'inbox'),(12,2,'b','b','4',1,'inbox');
    )sql",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    WatchProgressStore store(db);
    REQUIRE_FALSE(store.list(2).front().watched);
    auto s = store.begin(11, false);
    store.sample(11, s.id, 40, 120, {{1, 40}});
    REQUIRE(store.resumePosition(11) == 40);
    REQUIRE_FALSE(store.resumePosition(12).has_value());
    store.mark(11, true);
    REQUIRE(sqlite3_exec(db.handle(), "UPDATE media_file SET anime_id=2 WHERE id=11; DELETE FROM anime WHERE id=1;",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(store.list(2).size() == 2);
    REQUIRE(store.list(2)[1].watched);
    REQUIRE(store.list(2)[0].position == 40);
    REQUIRE(store.list(2)[1].position == 0);
    REQUIRE(store.resumePosition(11) == 0);
    REQUIRE(store.mark(12, false).position == 0);
    REQUIRE_FALSE(store.list(2)[0].watched);
    REQUIRE_THROWS_AS(store.mark(99, true), std::out_of_range);
}
