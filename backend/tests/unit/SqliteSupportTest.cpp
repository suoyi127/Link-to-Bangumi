#include "SqliteSupport.hpp"
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace anime_vault::sqlite_support;
namespace {
struct MemoryDatabase {
    sqlite3* db{};
    MemoryDatabase() { if (sqlite3_open(":memory:", &db) != SQLITE_OK) throw std::runtime_error("open failed"); }
    ~MemoryDatabase() { sqlite3_close(db); }
};
}
TEST_CASE("sqlite support rolls back uncommitted writes including exception unwinding") {
    MemoryDatabase database;
    exec(database.db, "CREATE TABLE items(value TEXT UNIQUE)");
    REQUIRE_THROWS_AS(([&] {
        Transaction transaction(database.db);
        auto insert = prepare(database.db, "INSERT INTO items VALUES(?)");
        anime_vault::sqlite_support::bind(insert.get(), 1, std::string("temporary")); done(database.db, insert.get());
        throw std::runtime_error("stop");
    }()), std::runtime_error);
    auto count = prepare(database.db, "SELECT count(*) FROM items");
    REQUIRE(sqlite3_step(count.get()) == SQLITE_ROW);
    CHECK(sqlite3_column_int(count.get(), 0) == 0);
    { Transaction transaction(database.db); exec(database.db, "INSERT INTO items VALUES('kept')"); transaction.commit(); }
    auto kept = prepare(database.db, "SELECT value FROM items");
    REQUIRE(sqlite3_step(kept.get()) == SQLITE_ROW);
    CHECK(column(kept.get(), 0) == "kept");
}
TEST_CASE("sqlite support reports prepare bind and step failures") {
    MemoryDatabase database;
    CHECK_THROWS_AS(prepare(database.db, "SELECT FROM"), std::runtime_error);
    exec(database.db, "CREATE TABLE items(value TEXT UNIQUE)");
    auto insert = prepare(database.db, "INSERT INTO items VALUES(?)");
    CHECK_THROWS_AS(anime_vault::sqlite_support::bind(insert.get(), 2, std::string("invalid")), std::runtime_error);
    anime_vault::sqlite_support::bind(insert.get(), 1, std::string("duplicate")); done(database.db, insert.get());
    auto duplicate = prepare(database.db, "INSERT INTO items VALUES('duplicate')");
    CHECK_THROWS_AS(done(database.db, duplicate.get()), std::runtime_error);
    CHECK(sqlite3_get_autocommit(database.db) == 1);
}
