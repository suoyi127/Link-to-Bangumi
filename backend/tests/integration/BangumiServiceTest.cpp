#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/services/AnimeEnricher.hpp"
#include "anime_vault/services/BangumiCalendarService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <random>

using namespace anime_vault;

namespace {
struct FakeTransport final : BangumiTransport {
    int calls{};
    bool delay{};
    bool throwOnSearch{};
    Completion pending;
    std::optional<Response> next{Response{200,
        R"({"data":[{"id":123,"type":2,"name":"Frieren","name_cn":"葬送的芙莉莲","date":"2023-09-29","eps":28,"images":{"large":"https://example.test/cover.jpg"}}]})"}};
    std::optional<Response> subjectNext;
    std::optional<Response> episodesNext;
    int episodeCalls{};
    int subjectCalls{};
    std::vector<Response> episodePages;
    void episodes(std::int64_t, int, Completion completion) {
        ++episodeCalls;
        if (!episodePages.empty()) {
            const auto response = episodePages.front();
            episodePages.erase(episodePages.begin());
            completion(response, ""); return;
        }
        completion(episodesNext, episodesNext ? "" : "offline");
    }
    std::optional<Response> aliasNext{Response{200, R"({"results":0,"list":[]})"}};
    std::optional<Response> aliasPrefixNext;
    std::vector<std::string> aliasQueries;
    int calendarCalls{};
    std::optional<Response> calendarNext;
    void calendar(Completion completion) override {
        ++calendarCalls;
        completion(calendarNext, calendarNext ? "" : "offline");
    }
    void search(std::string, Completion completion) override {
        ++calls;
        if (throwOnSearch) throw std::runtime_error("transport exception with private details");
        if (delay) { pending = std::move(completion); return; }
        completion(next, next ? "" : "timeout");
    }
    void subject(std::int64_t, Completion completion) override { ++subjectCalls; completion(subjectNext, subjectNext ? "" : "timeout"); }
    void searchAliases(std::string keyword, Completion completion) override {
        aliasQueries.push_back(std::move(keyword));
        const auto& response = aliasQueries.size() > 1 && aliasPrefixNext ? aliasPrefixNext : aliasNext;
        completion(response, response ? "" : "timeout");
    }
};
struct TempDb {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("anime-vault-bangumi-" + std::to_string(std::random_device{}()) + ".db");
    ~TempDb() { std::error_code ec; std::filesystem::remove(path, ec); }
};
}

TEST_CASE("Bangumi subject lookup rejects a response for a different requested ID") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    transport.subjectNext = FakeTransport::Response{200, R"({"id":999,"type":2,"name":"Other"})"};
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSubjectResult> result;
    service->subject(123, [&](BangumiSubjectResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->errorCode == "bangumi_bad_response");
    REQUIRE_FALSE(result->subject);
}

TEST_CASE("bound Bangumi episode sort becomes season episode while manual edits win") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto scan = repo.createScan({0, "test", "completed"});
    const auto media = repo.insertMedia({0, scan, "C:/test/Ao no Hako Season 2 - 27.mkv", "Ao no Hako Season 2 - 27.mkv", "27", "normal", 1, "inbox", .95});
    repo.updateMediaCorrection(media, {"Ao no Hako Season 2", "2", "27", "normal", .95, std::nullopt}, MediaCorrectionIntent::automaticScan);
    FakeTransport transport;
    transport.subjectNext = FakeTransport::Response{200, R"({"id":548156,"type":2,"name":"Ao no Hako Season 2","eps":12})"};
    transport.episodesNext = FakeTransport::Response{200, R"({"total":2,"offset":0,"data":[{"type":0,"sort":26,"ep":1},{"type":0,"sort":27,"ep":2}]})"};
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSubjectResult> result;
    service->subject(548156, [&](auto value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->subject);
    const auto anime = *repo.getMedia(media)->animeId;
    repo.bindAnime(anime, *result->subject);
    REQUIRE(repo.getMedia(media)->episodeNumber == "2");
    REQUIRE(repo.getMedia(media)->rawEpisodeNumber == "27");
    REQUIRE(repo.getMedia(media)->episodeNumberSource == "bangumi");
    repo.bindAnime(anime, *result->subject);
    REQUIRE(repo.getMedia(media)->episodeNumber == "2");
    repo.updateMediaEpisode(media, "7", "normal");
    repo.bindAnime(anime, *result->subject);
    REQUIRE(repo.getMedia(media)->episodeNumber == "7");
    REQUIRE(repo.getMedia(media)->rawEpisodeNumber == "27");
    REQUIRE(repo.getMedia(media)->episodeNumberSource == "manual");
    repo.updateMediaCorrection(media, {"Ao no Hako Season 2", "2", "27", "normal", .95, std::nullopt}, MediaCorrectionIntent::automaticScan);
    REQUIRE(repo.getMedia(media)->episodeNumber == "7");
    const auto later = repo.insertMedia({0, scan, "C:/test/later.mkv", "later.mkv", "26", "normal", 1, "inbox", .95});
    repo.updateMediaCorrection(later, {"Ao no Hako Season 2", "2", "26", "normal", .95, std::nullopt}, MediaCorrectionIntent::automaticScan);
    REQUIRE(repo.getMedia(later)->episodeNumber == "1");
    REQUIRE(repo.getMedia(later)->rawEpisodeNumber == "26");
    repo.bindAnime(anime, {999, "Other season", "", "", "", 12, 2});
    REQUIRE(repo.getMedia(later)->episodeNumber == "26");
    REQUIRE(repo.getMedia(media)->episodeNumber == "7");
    REQUIRE_FALSE(repo.updateEpisodeMappingsIfBound(anime, *result->subject));
    REQUIRE(repo.getMedia(later)->episodeNumber == "26");
}

TEST_CASE("scans refresh episode mapping for an already bound anime") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto scan = repo.createScan({0, "test", "completed"});
    const auto media = repo.insertMedia({0, scan, "C:/test/bound.mkv", "bound.mkv", "27", "normal", 1, "inbox", .95});
    repo.updateMediaCorrection(media, {"Ao no Hako Season 2", "2", "27", "normal", .95, std::nullopt}, MediaCorrectionIntent::automaticScan);
    repo.bindAnime(*repo.getMedia(media)->animeId, {548156, "Ao no Hako Season 2", "", "", "", 12, 2});
    FakeTransport transport;
    transport.subjectNext = FakeTransport::Response{200, R"({"id":548156,"type":2,"name":"Ao no Hako Season 2"})"};
    transport.episodesNext = FakeTransport::Response{200, R"({"total":1,"offset":0,"data":[{"type":0,"sort":27,"ep":2}]})"};
    auto service = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, service);
    enricher->runForScan(scan);
    REQUIRE(repo.getMedia(media)->episodeNumber == "2");
    enricher->runForScan(scan);
    REQUIRE(transport.episodeCalls == 1);
}

TEST_CASE("explicit season files reuse a legacy binding with an unset season") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto scan = repo.createScan({0, "test", "completed"});
    const auto first = repo.insertMedia({0, scan, "C:/test/legacy.mkv", "legacy.mkv", "26", "normal", 1, "inbox", .95});
    repo.updateMediaCorrection(first, {"Ao no Hako Season 2", "", "26", "normal", .95, std::nullopt}, MediaCorrectionIntent::automaticScan);
    const auto anime = *repo.getMedia(first)->animeId;
    BangumiSubject subject{548156, "Ao no Hako Season 2", "", "", "", 12, 2};
    subject.episodes = {{"26", "1"}, {"27", "2"}};
    repo.bindAnime(anime, subject);
    const auto later = repo.insertMedia({0, scan, "C:/test/legacy-later.mkv", "legacy-later.mkv", "27", "normal", 1, "inbox", .95});
    repo.updateMediaCorrection(later, {"Ao no Hako Season 2", "2", "27", "normal", .95, std::nullopt}, MediaCorrectionIntent::automaticScan);
    REQUIRE(repo.getMedia(later)->animeId == anime);
    REQUIRE(repo.getMedia(later)->episodeNumber == "2");
}

TEST_CASE("automatic special episodes remain unconverted and can be scanned") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto scan = repo.createScan({0, "test", "completed"});
    const auto media = repo.insertMedia({0, scan, "C:/test/special.mkv", "special.mkv", "27", "sp", 1, "inbox", .95});
    REQUIRE_NOTHROW(repo.updateMediaCorrection(media, {"Special show", "2", "27", "sp", .95, std::nullopt}, MediaCorrectionIntent::automaticScan));
    REQUIRE(repo.getMedia(media)->episodeNumber == "27");
}

TEST_CASE("background enrichment retries bound episode mapping after a failed initial load") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const auto scan = repo.createScan({0, "test", "completed"});
    const auto media = repo.insertMedia({0, scan, "C:/test/retry.mkv", "retry.mkv", "27", "normal", 1, "inbox", .95});
    repo.updateMediaCorrection(media, {"Ao no Hako Season 2", "2", "27", "normal", .95, std::nullopt}, MediaCorrectionIntent::automaticScan);
    repo.bindAnime(*repo.getMedia(media)->animeId, {548156, "Ao no Hako Season 2", "", "", "", 12, 2});
    FakeTransport transport;
    transport.episodesNext = FakeTransport::Response{200, R"({"total":1,"offset":0,"data":[{"type":0,"sort":27,"ep":2}]})"};
    auto service = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, service);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO anime(display_title) VALUES('Unmatched A'),('Unmatched B'),('Unmatched C'),('Unmatched D'),('Unmatched E');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    enricher->runOnce();
    REQUIRE(repo.getMedia(media)->episodeNumber == "2");
}

TEST_CASE("episode mappings require complete unambiguous normal episode pages") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    transport.episodePages = {{200, R"({"total":2,"offset":0,"data":[{"type":0,"sort":26,"ep":1}]})"},
                              {200, R"({"total":2,"offset":1,"data":[{"type":0,"sort":27,"ep":2}]})"}};
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSubjectResult> result;
    service->episodeMapping(548156, [&](auto value) { result = std::move(value); });
    REQUIRE(result->subject);
    REQUIRE(result->subject->episodes.size() == 2);
    REQUIRE(result->subject->episodes[1].sort == "27");
    REQUIRE(result->subject->episodes[1].ep == "2");
    REQUIRE(transport.episodeCalls == 2);
    for (const auto& invalid : std::vector<std::string>{
        R"({"total":2,"offset":0,"data":[{"type":0,"sort":27,"ep":2},{"type":0,"sort":27,"ep":3}]})",
        R"({"total":2,"offset":0,"data":[{"type":0,"sort":27,"ep":2},{"type":0,"sort":28,"ep":2}]})",
        R"({"total":1,"offset":0,"data":[{"type":1,"sort":27,"ep":2}]})",
        R"({"total":1,"offset":0,"data":[{"type":0,"sort":27,"ep":0}]})",
        R"({"total":1,"offset":9,"data":[{"type":0,"sort":27,"ep":2}]})",
        R"({"total":1001,"offset":0,"data":[]})"}) {
        CAPTURE(invalid);
        transport.episodesNext = FakeTransport::Response{200, invalid};
        service = std::make_shared<BangumiService>(repo, transport);
        service->episodeMapping(548156, [&](auto value) { result = std::move(value); });
        REQUIRE(result->errorCode.empty());
        REQUIRE(result->subject);
        REQUIRE(result->subject->episodes.empty());
    }
    transport.episodePages = {{200, R"({"total":2,"offset":0,"data":[{"type":0,"sort":26,"ep":1}]})"}, {503, "offline"}};
    service = std::make_shared<BangumiService>(repo, transport);
    service->episodeMapping(548156, [&](auto value) { result = std::move(value); });
    REQUIRE(result->subject->episodes.empty());
}

TEST_CASE("calendar intersects all local pages by subject ID and rechecks media on cache reads") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO anime(display_title,bangumi_subject_id) VALUES('Local show',123);"
        "INSERT INTO media_file(anime_id,source_path,filename,size_bytes,status) VALUES(1,'C:/test/show.mkv','show.mkv',1,'inbox');"
        "INSERT INTO anime(display_title,bangumi_subject_id) VALUES('Metadata only',789);"
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<201) "
        "INSERT INTO anime(display_title) SELECT printf('Other %d',x) FROM n;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.calendarNext = FakeTransport::Response{200,
        R"([{"weekday":{"id":1},"items":[{"id":123,"type":2},{"id":123,"type":2},{"id":789,"type":2}]},{"weekday":{"id":2},"items":[{"id":999,"type":2}]}])"};
    auto service = std::make_shared<BangumiCalendarService>(repo, transport);
    std::optional<AnimeCalendarResult> result;
    service->get([&](AnimeCalendarResult value) { result = std::move(value); });
    REQUIRE(result->errorCode.empty());
    REQUIRE(result->days[0].items.size() == 1);
    REQUIRE(result->days[0].items[0].id == 1);
    REQUIRE(result->days[1].items.empty());
    REQUIRE(result->days[6].weekday == 7);
    REQUIRE_FALSE(result->fromCache);
    REQUIRE(sqlite3_exec(db.handle(), "UPDATE media_file SET status='missing' WHERE anime_id=1;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    service->get([&](AnimeCalendarResult value) { result = std::move(value); });
    REQUIRE(result->fromCache);
    REQUIRE(result->days[0].items.empty());
    REQUIRE(transport.calendarCalls == 1);
}

TEST_CASE("calendar uses recent stale data on failure but rejects invalid weekdays and ancient caches") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    transport.calendarNext = FakeTransport::Response{200, R"([{"weekday":{"id":3},"items":[]}])"};
    auto now = std::chrono::system_clock::time_point{std::chrono::seconds{1000000}};
    auto service = std::make_shared<BangumiCalendarService>(repo, transport, [&] { return now; });
    std::optional<AnimeCalendarResult> result;
    service->get([&](AnimeCalendarResult value) { result = std::move(value); });
    REQUIRE(result->errorCode.empty());
    now += std::chrono::hours{7};
    transport.calendarNext = FakeTransport::Response{200, R"([{"weekday":{"id":8},"items":[]}])"};
    service->get([&](AnimeCalendarResult value) { result = std::move(value); });
    REQUIRE(result->stale);
    REQUIRE(result->errorCode == "bangumi_calendar_bad_response");
    REQUIRE(result->days[2].weekday == 3);
    now += std::chrono::hours{24 * 8};
    transport.calendarNext.reset();
    service->get([&](AnimeCalendarResult value) { result = std::move(value); });
    REQUIRE_FALSE(result->stale);
    REQUIRE(result->errorCode == "bangumi_calendar_unavailable");
}

TEST_CASE("local bound title resolves before Bangumi transport") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Local title');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repo.bindAnime(1, {123, "Frieren", "葬送的芙莉莲", "", "", 28, 2});
    FakeTransport transport;
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSearchResult> result;
    service->search({"葬送 的 芙莉莲", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->localMatch);
    REQUIRE(result->localMatch->id == 1);
    REQUIRE(result->localMatch->bangumiSubjectId == 123);
    REQUIRE(transport.calls == 0);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO anime(display_title) VALUES('Other');"
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(2,'葬送的芙莉莲','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    result.reset();
    service->search({"葬送的芙莉莲", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE_FALSE(result->localMatch);
    REQUIRE(transport.calls == 1);
}

TEST_CASE("unbound local parsed title still searches Bangumi") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Frieren');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSearchResult> result;
    service->search({"Frieren", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE_FALSE(result->localMatch);
    REQUIRE(transport.calls == 1);
}

TEST_CASE("automatic enrichment binds only a unique high confidence Bangumi match") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Frieren');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.subjectNext = FakeTransport::Response{200,
        R"({"id":123,"type":2,"name":"Frieren","name_cn":"葬送的芙莉莲","date":"2023-09-29","eps":28,"images":{"large":"https://example.test/cover.jpg"}})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    std::size_t bound = 0;
    enricher->runOnce(1, [&](std::size_t count) { bound = count; });
    const auto anime = repo.getAnime(1);
    REQUIRE(anime);
    REQUIRE(anime->bangumiSubjectId == 123);
    REQUIRE(anime->coverUrl == "https://example.test/cover.jpg");
    REQUIRE(bound == 1);
}

TEST_CASE("scan enrichment targets its own anime even when older unbound entries exceed the batch limit") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO scan_job(source,status) VALUES('folder_import','completed');"
        "INSERT INTO anime(display_title) VALUES('Frieren');"
        "INSERT INTO media_file(scan_id,anime_id,source_path,filename,size_bytes,status) "
        "VALUES(1,1,'C:/folder/Frieren - 01.mkv','Frieren - 01.mkv',1,'inbox');"
        "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<6) "
        "INSERT INTO anime(display_title) SELECT printf('Unrelated %d',x) FROM n;",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.subjectNext = FakeTransport::Response{200,
        R"({"id":123,"type":2,"name":"Frieren","name_cn":"葬送的芙莉莲"})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    std::size_t bound = 0;
    enricher->runForScan(1, [&](std::size_t count) { bound = count; });
    REQUIRE(bound == 1);
    REQUIRE(repo.getAnime(1)->bangumiSubjectId == 123);
    REQUIRE(transport.calls == 1);
}

TEST_CASE("automatic enrichment can bind a romanized title through a Bangumi alias") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Saijo no Osewa');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.next = FakeTransport::Response{200, R"({"data":[]})"};
    transport.aliasNext = FakeTransport::Response{200, R"({"results":1,"list":[{"id":602733}]})"};
    transport.subjectNext = FakeTransport::Response{200,
        R"({"id":602733,"type":2,"name":"Japanese title","name_cn":"才女的侍从","infobox":[{"key":"别名","value":[{"v":"Saijo no Osewa: Full Title"}]}]})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    std::size_t bound = 0;
    enricher->runOnce(1, [&](std::size_t count) { bound = count; });
    const auto anime = repo.getAnime(1);
    REQUIRE(anime);
    REQUIRE(bound == 1);
    REQUIRE(anime->bangumiSubjectId == 602733);
    REQUIRE(anime->displayTitle == "才女的侍从");
}

TEST_CASE("Bangumi alias fallback rejects ambiguous and unrelated subjects") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Saijo no Osewa');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.next = FakeTransport::Response{200, R"({"data":[]})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    transport.aliasNext = FakeTransport::Response{200,
        R"({"results":2,"list":[{"id":602733},{"id":123}]})"};
    enricher->runOnce(1, [](std::size_t count) { REQUIRE(count == 0); });
    REQUIRE_FALSE(repo.getAnime(1)->bangumiSubjectId);
    transport.aliasNext = FakeTransport::Response{200,
        R"({"results":1,"list":[{"id":602733}]})"};
    transport.subjectNext = FakeTransport::Response{200,
        R"({"id":602733,"type":2,"name":"Other","name_cn":"其他","infobox":[{"key":"别名","value":"Unrelated Show"}]})"};
    enricher->runOnce(1, [](std::size_t count) { REQUIRE(count == 0); });
    REQUIRE_FALSE(repo.getAnime(1)->bangumiSubjectId);
}

TEST_CASE("Bangumi alias fallback retries a long romanized prefix and accepts a minor spelling difference") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    const std::string title = "Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi o Shita";
    REQUIRE(sqlite3_exec(db.handle(), ("INSERT INTO anime(display_title) VALUES('" + title + "');").c_str(),
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.next = FakeTransport::Response{200, R"({"data":[]})"};
    transport.aliasNext = FakeTransport::Response{200, R"({"results":0,"list":[]})"};
    transport.aliasPrefixNext = FakeTransport::Response{200,
        R"({"results":1,"list":[{"id":607340}]})"};
    transport.subjectNext = FakeTransport::Response{200,
        R"({"id":607340,"type":2,"name":"Japanese title","name_cn":"与奔驰于透明之夜的你，谈一场看不见的恋爱。","infobox":[{"key":"别名","value":[{"v":"Toumei na Yoru ni Kakeru Kimi to, Me ni Mienai Koi wo Shita"}]}]})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    enricher->runOnce(1, [](std::size_t count) { REQUIRE(count == 1); });
    REQUIRE(transport.aliasQueries.size() == 2);
    REQUIRE(transport.aliasQueries[0] == title);
    REQUIRE(transport.aliasQueries[1] == "Toumei na Yoru ni Kakeru Kimi");
    REQUIRE(repo.getAnime(1)->bangumiSubjectId == 607340);
}

TEST_CASE("automatic Bangumi binding does not replace a user-edited display title") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO anime(display_title) VALUES('Saijo no Osewa');"
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(1,'mytitle','user');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.next = FakeTransport::Response{200, R"({"data":[]})"};
    transport.aliasNext = FakeTransport::Response{200,
        R"({"results":1,"list":[{"id":602733}]})"};
    transport.subjectNext = FakeTransport::Response{200,
        R"({"id":602733,"type":2,"name":"Japanese title","name_cn":"才女的侍从","infobox":[{"key":"别名","value":"Saijo no Osewa"}]})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    enricher->runOnce(1, [](std::size_t count) { REQUIRE(count == 1); });
    REQUIRE(repo.getAnime(1)->displayTitle == "Saijo no Osewa");
}

TEST_CASE("automatic Bangumi binding preserves a title established by Mikan") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO anime(display_title) VALUES('Mikan 中文标题');"
        "INSERT INTO anime_alias(anime_id,normalized_alias,source) VALUES(1,'romanalias','mikan');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.next = FakeTransport::Response{200,
        R"({"data":[{"id":602733,"type":2,"name":"Mikan 中文标题","name_cn":"Bangumi 中文标题"}]})"};
    transport.subjectNext = FakeTransport::Response{200,
        R"({"id":602733,"type":2,"name":"Mikan 中文标题","name_cn":"Bangumi 中文标题"})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    enricher->runOnce(1, [](std::size_t count) { REQUIRE(count == 1); });
    REQUIRE(repo.getAnime(1)->displayTitle == "Mikan 中文标题");
}

TEST_CASE("automatic enrichment leaves ambiguous titles unbound") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Same');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeTransport transport;
    transport.next = FakeTransport::Response{200,
        R"({"data":[{"id":123,"type":2,"name":"Same"},{"id":124,"type":2,"name":"Same"}]})"};
    auto bangumi = std::make_shared<BangumiService>(repo, transport);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi);
    std::size_t bound = 99;
    enricher->runOnce(1, [&](std::size_t count) { bound = count; });
    REQUIRE(bound == 0);
    REQUIRE_FALSE(repo.getAnime(1)->bangumiSubjectId);
}

TEST_CASE("seasonless search does not short-circuit to a season-specific local binding") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title,season) VALUES('Show','1');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repo.bindAnime(1, {123, "Show", "", "", "", 12, 2});
    FakeTransport transport;
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSearchResult> result;
    service->search({"Show", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE_FALSE(result->localMatch);
    REQUIRE(transport.calls == 1);
}

TEST_CASE("Bangumi transport exception completes search once with a sanitized error") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    transport.throwOnSearch = true;
    auto service = std::make_shared<BangumiService>(repo, transport);
    int callbacks = 0;
    std::string code;
    service->search({"Frieren", {}, {}}, [&](BangumiSearchResult value) {
        ++callbacks;
        code = std::move(value.errorCode);
    });
    REQUIRE(callbacks == 1);
    REQUIRE(code == "bangumi_unavailable");
}

TEST_CASE("Bangumi cache-hit callback exception is not delivered a second time") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    auto service = std::make_shared<BangumiService>(repo, transport);
    service->search({"Frieren", {}, {}}, [](BangumiSearchResult) {});
    int callbacks = 0;
    REQUIRE_THROWS_AS(service->search({"frieren", {}, {}}, [&](BangumiSearchResult) {
        ++callbacks;
        throw std::runtime_error("consumer failed");
    }), std::runtime_error);
    REQUIRE(callbacks == 1);
    REQUIRE(transport.calls == 1);
}

TEST_CASE("late failed Bangumi request cannot replace a concurrent successful cache entry") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    transport.delay = true;
    std::chrono::system_clock::time_point now{std::chrono::seconds{1'700'000'000}};
    auto service = std::make_shared<BangumiService>(repo, transport, [&] { return now; });
    std::optional<BangumiSearchResult> first, second, cached;
    service->search({"Frieren", {}, {}}, [&](BangumiSearchResult value) { first = std::move(value); });
    const auto success = transport.pending;
    service->search({"Frieren", {}, {}}, [&](BangumiSearchResult value) { second = std::move(value); });
    const auto failure = transport.pending;
    REQUIRE(transport.calls == 2);
    success(transport.next, "");
    failure(std::nullopt, "timeout");
    REQUIRE(first->errorCode.empty());
    REQUIRE(second->errorCode == "bangumi_unavailable");
    service->search({"Frieren", {}, {}}, [&](BangumiSearchResult value) { cached = std::move(value); });
    REQUIRE(cached->errorCode.empty());
    REQUIRE(cached->fromCache);
    REQUIRE(cached->candidates.items[0].id == 123);
    REQUIRE(transport.calls == 2);
}

TEST_CASE("Bangumi search rejects invalid and malformed input without leaking remote bodies") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSearchResult> result;
    service->search({" ", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result->errorCode == "bangumi_invalid_query");
    REQUIRE(transport.calls == 0);
    result.reset();
    service->search({std::string(501, 'a'), {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result->errorCode == "bangumi_invalid_query");
    REQUIRE(transport.calls == 0);
    transport.next = FakeTransport::Response{200, R"({"data":[{"id":0,"type":2,"name":"private-remote-body"}]})"};
    result.reset();
    service->search({"valid", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result->errorCode == "bangumi_bad_response");
    REQUIRE(result->errorCode.find("private-remote-body") == std::string::npos);
    REQUIRE(transport.calls == 1);
    transport.next = FakeTransport::Response{200, R"({"data":[{"id":123,"type":2,"name":"   ","name_cn":"valid"}]})"};
    result.reset();
    service->search({"valid2", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result->errorCode == "bangumi_bad_response");
}

TEST_CASE("Bangumi search accepts an exact long Chinese anime title") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    const std::string title = "才女的侍从 在满是高岭之花的贵族学校暗中照顾（毫无生活自理能力的）学院第一大小姐";
    REQUIRE(title.size() > 100);
    transport.next = FakeTransport::Response{200,
        R"({"data":[{"id":602733,"type":2,"name":"才女のお世話","name_cn":"才女的侍从 在满是高岭之花的贵族学校暗中照顾（毫无生活自理能力的）学院第一大小姐"}]})"};
    auto service = std::make_shared<BangumiService>(repo, transport);
    std::optional<BangumiSearchResult> result;
    service->search({title}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->errorCode.empty());
    REQUIRE(transport.calls == 1);
    REQUIRE(result->candidates.autoBindEligible);
    REQUIRE(result->candidates.items[0].id == 602733);
}

TEST_CASE("Bangumi asynchronous completion retains service state and respects the request limit") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    transport.delay = true;
    std::chrono::system_clock::time_point now{std::chrono::seconds{1'700'000'000}};
    auto service = std::make_shared<BangumiService>(repo, transport, [&] { return now; });
    std::optional<BangumiSearchResult> first;
    service->search({"Frieren", {}, {}}, [&](BangumiSearchResult value) { first = std::move(value); });
    REQUIRE(transport.calls == 1);
    service.reset();
    transport.pending(transport.next, "");
    REQUIRE(first);
    REQUIRE(first->errorCode.empty());

    auto limited = std::make_shared<BangumiService>(repo, transport, [&] { return now; });
    transport.delay = false;
    transport.next = FakeTransport::Response{200, R"({"data":[]})"};
    for (int index = 0; index < 10; ++index)
        limited->search({"unique" + std::to_string(index), {}, {}}, [](BangumiSearchResult) {});
    std::optional<BangumiSearchResult> result;
    limited->search({"last", {}, {}}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result->errorCode == "bangumi_local_rate_limited");
    REQUIRE(transport.calls == 11);
}

TEST_CASE("Bangumi search caches normalized queries and retries offline without affecting SQLite") {
    TempDb temp;
    SqliteDatabase db(temp.path);
    db.migrate();
    SqliteMediaRepository repo(db);
    FakeTransport transport;
    std::chrono::system_clock::time_point now{std::chrono::seconds{1'700'000'000}};
    auto service = std::make_shared<BangumiService>(repo, transport, [&] { return now; });
    std::optional<BangumiSearchResult> result;
    service->search({"Frieren", std::nullopt, std::nullopt}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->errorCode.empty());
    REQUIRE(result->candidates.items.size() == 1);
    REQUIRE(result->candidates.items[0].id == 123);
    REQUIRE(transport.calls == 1);

    result.reset();
    service->search({"  frieren  ", std::nullopt, std::nullopt}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->fromCache);
    REQUIRE(transport.calls == 1);

    now += std::chrono::hours{25};
    transport.next.reset();
    result.reset();
    service->search({"Frieren", std::nullopt, std::nullopt}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->errorCode == "bangumi_unavailable");
    REQUIRE(result->retryCount == 1);
    REQUIRE(result->retryAfter);
    REQUIRE(transport.calls == 2);
    REQUIRE(repo.listInbox().empty());

    result.reset();
    service->search({"Frieren", std::nullopt, std::nullopt}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result->errorCode == "bangumi_unavailable");
    REQUIRE(transport.calls == 2);

    now += std::chrono::minutes{1};
    transport.next = FakeTransport::Response{200, R"({"data":[]})"};
    result.reset();
    service->search({"Frieren", std::nullopt, std::nullopt}, [&](BangumiSearchResult value) { result = std::move(value); });
    REQUIRE(result->errorCode.empty());
    REQUIRE(transport.calls == 3);
}

TEST_CASE("Bangumi reuses validated subject details without spending another request") {
    SqliteDatabase db(":memory:"); db.migrate(); SqliteMediaRepository repo(db);
    FakeTransport transport;
    transport.subjectNext = BangumiTransport::Response{200, R"({"id":123,"type":2,"name":"Frieren"})"};
    auto service = std::make_shared<BangumiService>(repo, transport);
    service->subject(123, [](auto result) { REQUIRE(result.subject); });
    service->subject(123, [](auto result) { REQUIRE(result.subject); });
    CHECK(transport.subjectCalls == 1);
}

TEST_CASE("Bangumi queues background work and reserves manual requests until the window resets") {
    SqliteDatabase db(":memory:"); db.migrate(); SqliteMediaRepository repo(db);
    FakeTransport transport; transport.next = BangumiTransport::Response{200, R"({"data":[]})"};
    auto now = std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};
    std::vector<std::function<void()>> timers;
    auto service = std::make_shared<BangumiService>(repo, transport, [&] { return now; },
        [&](auto, auto action) { timers.push_back(std::move(action)); });
    int completed = 0;
    bool manualFinished = false;
    for (int i = 0; i < 9; ++i) service->search({"background" + std::to_string(i)}, [&, i](auto result) { CHECK(result.errorCode.empty()); if (i == 8) CHECK(manualFinished); ++completed; }, BangumiService::Priority::background);
    CHECK(transport.calls == 8); CHECK(completed == 8);
    service->search({"manual1"}, [&](auto result) { CHECK(result.errorCode.empty()); ++completed; });
    service->search({"manual2"}, [&](auto result) { CHECK(result.errorCode.empty()); ++completed; });
    CHECK(transport.calls == 10);
    service->search({"manual3"}, [&](auto result) { CHECK(result.errorCode.empty()); manualFinished = true; });
    CHECK_FALSE(manualFinished);
    now += std::chrono::seconds{60};
    auto wake = std::move(timers.front()); timers.erase(timers.begin()); wake();
    CHECK(manualFinished); CHECK(completed == 11); CHECK(transport.calls == 12);
}

TEST_CASE("Bangumi upstream 429 is distinct and pauses subsequent queued sends") {
    SqliteDatabase db(":memory:"); db.migrate(); SqliteMediaRepository repo(db);
    FakeTransport transport; transport.next = BangumiTransport::Response{429, ""};
    auto now = std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};
    std::vector<std::function<void()>> timers;
    auto service = std::make_shared<BangumiService>(repo, transport, [&] { return now; }, [&](auto, auto action) { timers.push_back(std::move(action)); });
    service->search({"first"}, [](auto result) { CHECK(result.errorCode == "bangumi_remote_rate_limited"); CHECK(result.retryAfter); });
    transport.next = BangumiTransport::Response{200, R"({"data":[]})"};
    bool completed = false;
    service->search({"second"}, [&](auto result) { CHECK(result.errorCode.empty()); completed = true; });
    CHECK_FALSE(completed); CHECK(transport.calls == 1);
    now += std::chrono::seconds{60}; auto wake = std::move(timers.front()); timers.erase(timers.begin()); wake();
    CHECK(completed); CHECK(transport.calls == 2);
}
