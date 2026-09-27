#include "anime_vault/services/BangumiService.hpp"
#include "anime_vault/services/AnimeEnricher.hpp"
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
    void search(std::string, Completion completion) override {
        ++calls;
        if (throwOnSearch) throw std::runtime_error("transport exception with private details");
        if (delay) { pending = std::move(completion); return; }
        completion(next, next ? "" : "timeout");
    }
    void subject(std::int64_t, Completion completion) override { completion(subjectNext, subjectNext ? "" : "timeout"); }
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
    REQUIRE(result->errorCode == "bangumi_rate_limited");
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
