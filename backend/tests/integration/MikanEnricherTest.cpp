#include "anime_vault/services/MikanEnricher.hpp"
#include "anime_vault/services/FilenameParser.hpp"
#include "anime_vault/services/MikanTitleParser.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <random>

using namespace anime_vault;

TEST_CASE("Mikan enrichment adopts a matching RSS alias after a local anime exists") {
    const auto path = std::filesystem::temp_directory_path() /
        ("anime-vault-enrichment-" + std::to_string(std::random_device{}()) + ".db");
    {
        SqliteDatabase database(path);
        database.migrate();
        SqliteMediaRepository repository(database);
        REQUIRE(sqlite3_exec(database.handle(),
            "INSERT INTO anime(display_title) VALUES('Roman Title');", nullptr, nullptr, nullptr) == SQLITE_OK);
        MikanEnricher enricher(repository, [](MikanEnricher::CatalogCompletion completion) {
            completion({1, 1, {{"中文标题", "Roman Title", 3}}, ""});
        });
        bool done = false;
        enricher.runOnce([&](MikanEnrichmentResult result) {
            done = true;
            REQUIRE(result.appliedCount == 1);
            REQUIRE(result.errorCode.empty());
        });
        REQUIRE(done);
        REQUIRE(repository.getAnime(1)->displayTitle == "中文标题");
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST_CASE("Mikan enrichment keeps all explicit names regardless of RSS alias order") {
    SqliteDatabase database(":memory:");
    database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(), "INSERT INTO anime(display_title) VALUES('Anne_Shirley');", nullptr, nullptr, nullptr) == SQLITE_OK);
    MikanEnricher enricher(repository, [](MikanEnricher::CatalogCompletion completion) {
        completion({1, 1, {{"安妮的童话", "安妮·雪莉", 17}, {"安妮的童话", "Anne Shirley", 17}}, ""});
    });
    bool done = false;
    enricher.runOnce([&](MikanEnrichmentResult result) { done = true; CHECK(result.appliedCount == 2); });
    REQUIRE(done);
    REQUIRE(repository.findAnimeByTitle("安妮·雪莉"));
    CHECK(repository.findAnimeByTitle("Anne Shirley")->displayTitle == "安妮的童话");
}

TEST_CASE("sampled bracketed bilingual filename matches RSS then resolves its romanized alias") {
    const auto parsed = FilenameParser{}.parse("【悠哈璃羽字幕社】[暗芝居 第十七季_Yami Shibai 17][ 12 ][1080P].mkv");
    REQUIRE(parsed.episode == EpisodeNumber::parse("12"));
    SqliteDatabase database(":memory:");
    database.migrate();
    SqliteMediaRepository repository(database);
    sqlite3_stmt* insert = nullptr;
    REQUIRE(sqlite3_prepare_v2(database.handle(), "INSERT INTO anime(display_title) VALUES(?)", -1, &insert, nullptr) == SQLITE_OK);
    sqlite3_bind_text(insert, 1, parsed.title.c_str(), -1, SQLITE_TRANSIENT);
    REQUIRE(sqlite3_step(insert) == SQLITE_DONE);
    sqlite3_finalize(insert);
    MikanEnricher enricher(repository, [](MikanEnricher::CatalogCompletion completion) {
        completion({1, 1, MikanTitleParser::parseAll("【悠哈璃羽字幕社】[暗芝居 第十七季_Yami Shibai 17][12][1080P]"), ""});
    });
    enricher.runOnce([](MikanEnrichmentResult result) { CHECK(result.appliedCount == 2); });
    const auto anime = repository.findAnimeByTitle("Yami.Shibai_17");
    REQUIRE(anime);
    CHECK(anime->displayTitle == "暗芝居 第十七季");
}

TEST_CASE("single feed season evidence is limited to local raw episodes") {
    SqliteDatabase database(":memory:"); database.migrate();
    SqliteMediaRepository repository(database);
    REQUIRE(sqlite3_exec(database.handle(), "INSERT INTO anime(display_title,season) VALUES('Kusuriya no Hitorigoto','1'); INSERT INTO media_file(anime_id,source_path,filename,size_bytes,status,episode_number,raw_episode_number) VALUES(1,'C:/temp/a.mkv','a.mkv',1,'inbox','1','1');", nullptr, nullptr, nullptr) == SQLITE_OK);
    MikanEnricher enricher(repository, [](MikanEnricher::CatalogCompletion done) {
        done({1,1,{{"药屋少女的呢喃 第三季","Kusuriya no Hitorigoto",49,true}},""});
    });
    enricher.runOnce([](MikanEnrichmentResult result) { CHECK(result.appliedCount == 0); });
    CHECK(repository.getAnime(1)->displayTitle == "Kusuriya no Hitorigoto");
    REQUIRE(sqlite3_exec(database.handle(), "UPDATE media_file SET raw_episode_number='49';", nullptr, nullptr, nullptr) == SQLITE_OK);
    enricher.runOnce([](MikanEnrichmentResult result) { CHECK(result.appliedCount == 0); });
    REQUIRE(sqlite3_exec(database.handle(), "UPDATE anime SET season='';", nullptr, nullptr, nullptr) == SQLITE_OK);
    enricher.runOnce([](MikanEnrichmentResult result) { CHECK(result.appliedCount == 1); });
    CHECK(repository.getAnime(1)->displayTitle == "药屋少女的呢喃 第三季");
}
