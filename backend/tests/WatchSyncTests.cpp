#include "anime_vault/services/WatchSyncService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <map>

using namespace anime_vault;

TEST_CASE("watch sync isolates accounts and maps specials before collecting with consent") {
    SqliteDatabase db(":memory:");
    db.migrate();
    auto sql = [&](const char* query) {
        REQUIRE(sqlite3_exec(db.handle(), query, nullptr, nullptr, nullptr) == SQLITE_OK);
    };
    sql("INSERT INTO anime(id,display_title,bangumi_subject_id) VALUES(1,'Show',100);"
        "INSERT INTO media_file(id,anime_id,source_path,filename,episode_number,episode_type,size_bytes,status) "
        "VALUES(1,1,'a','a','1','sp',1,'inbox');");
    std::map<std::int64_t, std::string> credentials;
    bool collected = false, ambiguous = false;
    int puts = 0, posts = 0;
    WatchSyncService service(
        db,
        [&](auto method, auto path, auto body, auto token) -> WatchResponse {
            if (path == "/v0/me")
                return {200,
                        token == "second" ? R"({"id":2,"username":"other"})" : R"({"id":1,"username":"user"})"};
            REQUIRE(token == "first");
            if (path.starts_with("/v0/episodes?"))
                return {
                    200,
                    ambiguous
                        ? R"({"total":2,"data":[{"subject_id":100,"type":1,"id":42,"sort":1},{"subject_id":100,"type":1,"id":43,"sort":1}]})"
                        : R"({"total":2,"data":[{"subject_id":100,"type":0,"id":41,"sort":1},{"subject_id":100,"type":1,"id":42,"sort":1}]})"};
            if (method == "GET" && path == "/v0/users/user/collections/100")
                return collected ? WatchResponse{200, R"({"subject_id":100,"type":2})"} : WatchResponse{404, ""};
            if (method == "POST" && path == "/v0/users/-/collections/100") {
                REQUIRE_FALSE(collected);
                REQUIRE(body == R"({"type":3})");
                collected = true;
                ++posts;
                return {204, ""};
            }
            if (method == "PUT" && path == "/v0/users/-/collections/-/episodes/42") {
                REQUIRE(body == R"({"type":2})");
                ++puts;
                return {204, ""};
            }
            FAIL("unexpected upstream request");
            return {};
        },
        [&](auto id) { return credentials[id]; }, [&](auto id, auto value) { credentials[id] = value; });
    service.configure({true, "first"});
    sql("INSERT INTO watch_progress(media_id,watched) VALUES(1,1)");
    service.processOne();
    REQUIRE(service.status().issues.at(0).errorCode == "subject_not_collected");
    service.configure({true, "second"});
    REQUIRE(service.status().pending == 0);
    service.configure({true, "first"});
    service.collect(1);
    service.processOne();
    REQUIRE(posts == 1);
    REQUIRE(puts == 1);
    REQUIRE(service.status().pending == 0);
    sql("UPDATE media_file SET episode_number_source='manual'; UPDATE watch_progress SET watched=0; UPDATE "
        "watch_progress SET watched=1;");
    ambiguous = true;
    service.processOne();
    REQUIRE(puts == 1);
    REQUIRE(service.status().issues.at(0).errorCode == "episode_ambiguous");
    REQUIRE_THROWS_AS(service.collect(1), WatchSyncError);
    sql("UPDATE watch_progress SET watched=0");
    REQUIRE(service.status().pending == 0);
}

TEST_CASE("revoked credentials retain later completions for the same account") {
    SqliteDatabase db(":memory:");
    db.migrate();
    auto sql = [&](const char* s) {
        REQUIRE(sqlite3_exec(db.handle(), s, nullptr, nullptr, nullptr) == SQLITE_OK);
    };
    sql("INSERT INTO anime(id,display_title,bangumi_subject_id) VALUES(1,'A',100);"
        "INSERT INTO media_file(id,anime_id,source_path,filename,episode_number,episode_type,size_bytes,status,"
        "episode_number_source,raw_episode_number) VALUES (1,1,'a','a','1','normal',1,'inbox','bangumi','13'),"
        "(2,1,'b','b','2','normal',1,'inbox','bangumi','14');");
    int writes = 0;
    bool authorized = false, collected = false;
    std::string credential;
    WatchSyncService service(
        db,
        [&](auto method, auto path, auto, auto) -> WatchResponse {
            if (path == "/v0/me")
                return {200, R"({"id":1,"username":"user"})"};
            if (!authorized)
                return {401, ""};
            if (path.starts_with("/v0/episodes?"))
                return {
                    200,
                    R"({"total":3,"data":[{"type":0,"id":11,"sort":13,"ep":1},{"type":0,"id":12,"sort":14,"ep":2},{"type":0,"id":20,"sort":1,"ep":6}]})"};
            if (method == "GET")
                return collected ? WatchResponse{200, R"({"subject_id":100,"type":4})"} : WatchResponse{404, ""};
            REQUIRE(method == "PUT");
            REQUIRE((path.ends_with("/11") || path.ends_with("/12")));
            ++writes;
            return {204, ""};
        },
        [&](auto) { return credential; }, [&](auto, const auto& value) { credential = value; });
    service.configure({true, "valid"});
    sql("INSERT INTO watch_progress(media_id,watched) VALUES(1,1);");
    service.processOne();
    REQUIRE_FALSE(service.status().connected);
    sql("INSERT INTO watch_progress(media_id,watched) VALUES(2,1)");
    REQUIRE(service.status().pending == 2);
    authorized = true;
    service.configure({true, "renewed"});
    service.processOne();
    REQUIRE(service.status().issues.at(0).errorCode == "subject_not_collected");
    // 用户在 Bangumi 端已收藏，确认操作不得覆盖原有收藏状态。
    collected = true;
    service.collect(1);
    service.processOne();
    service.processOne();
    REQUIRE(writes == 2);
    REQUIRE(service.status().pending == 0);
    service.configure({false, std::nullopt});
    sql("UPDATE watch_progress SET watched=0;UPDATE watch_progress SET watched=1");
    REQUIRE(credential.empty());
    REQUIRE(service.status().pending == 0);
}
