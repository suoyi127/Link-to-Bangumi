#include "anime_vault/services/CoverScraper.hpp"
#include "anime_vault/services/AnimeEnricher.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <filesystem>
#include <random>

using namespace anime_vault;

namespace {
struct TempData {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("anime-vault-cover-" + std::to_string(std::random_device{}()));
    TempData() { std::filesystem::create_directory(root); }
    ~TempData() { std::error_code error; std::filesystem::remove_all(root, error); }
};
struct FakeBangumi final : BangumiTransport {
    std::string body = R"({"id":123,"type":2,"name":"Show","images":{"large":"https://lain.bgm.tv/pic/cover/l/a.jpg"}})";
    void search(std::string, Completion completion) override {
        completion(Response{200, R"({"data":[{"id":123,"type":2,"name":"Show","images":{"large":"https://lain.bgm.tv/pic/cover/l/a.jpg"}}]})"}, "");
    }
    void searchAliases(std::string, Completion completion) override {
        completion(Response{200, R"({"results":0,"list":[]})"}, "");
    }
    void subject(std::int64_t, Completion completion) override { completion(Response{200, body}, ""); }
};
struct FakeImage final : CoverImageFetcher {
    std::optional<std::string> bytes = std::string("\xff\xd8\xff", 3) + "fake-jpeg";
    void fetch(std::string, Completion completion) override { completion(bytes, bytes ? "" : "unavailable"); }
};
}

TEST_CASE("automatic high-confidence binding starts cover caching") {
    TempData temp;
    SqliteDatabase db(temp.root / "test.db");
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Show');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    FakeBangumi remote;
    auto bangumi = std::make_shared<BangumiService>(repo, remote);
    FakeImage image;
    auto scraper = std::make_shared<CoverScraper>(repo, bangumi, image, temp.root);
    auto enricher = std::make_shared<AnimeEnricher>(repo, bangumi, scraper);
    std::size_t bound{};
    enricher->runOnce(1, [&](std::size_t count) { bound = count; });
    REQUIRE(bound == 1);
    REQUIRE(repo.getAnime(1)->coverUrl.starts_with("/api/covers/1/123/"));
}

TEST_CASE("cover scraper rejects foreign images and invalid image bodies") {
    REQUIRE_FALSE(CoverScraper::allowedImagePath("http://lain.bgm.tv/pic/cover/a.jpg"));
    REQUIRE_FALSE(CoverScraper::allowedImagePath("https://lain.bgm.tv.evil.test/pic/cover/a.jpg"));
    REQUIRE(CoverScraper::allowedImagePath("https://lain.bgm.tv/pic/cover/l/a.jpg") == "/pic/cover/l/a.jpg");
    REQUIRE_FALSE(CoverScraper::detectImage("<html>not an image</html>"));
    REQUIRE(CoverScraper::detectImage(std::string("\xff\xd8\xff", 3) + "jpeg")->extension == "jpg");
}

TEST_CASE("cover scraper caches a bound subject and exposes only its current image") {
    TempData temp;
    SqliteDatabase db(temp.root / "test.db");
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Show');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repo.bindAnime(1, {123, "Show", "", "", "", 12, 2});
    FakeBangumi remote;
    auto bangumi = std::make_shared<BangumiService>(repo, remote);
    FakeImage image;
    auto scraper = std::make_shared<CoverScraper>(repo, bangumi, image, temp.root);
    std::optional<CoverScrapeResult> result;
    scraper->refresh(1, [&](CoverScrapeResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->errorCode.empty());
    REQUIRE(result->coverUrl.starts_with("/api/covers/1/123/"));
    const auto cached = scraper->readCached(1, 123, result->coverUrl.substr(result->coverUrl.rfind('/') + 1));
    REQUIRE(cached);
    REQUIRE(cached->mimeType == "image/jpeg");
    REQUIRE(cached->bytes == *image.bytes);
    repo.bindAnime(1, {456, "Different", "", "", "", 12, 2});
    REQUIRE_FALSE(scraper->readCached(1, 123, result->coverUrl.substr(result->coverUrl.rfind('/') + 1)));
}

TEST_CASE("failed image fetch leaves the previous cover unchanged") {
    TempData temp;
    SqliteDatabase db(temp.root / "test.db");
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Show');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repo.bindAnime(1, {123, "Show", "", "", "https://lain.bgm.tv/original.jpg", 12, 2});
    FakeBangumi remote;
    auto bangumi = std::make_shared<BangumiService>(repo, remote);
    FakeImage image;
    image.bytes.reset();
    auto scraper = std::make_shared<CoverScraper>(repo, bangumi, image, temp.root);
    std::optional<CoverScrapeResult> result;
    scraper->refresh(1, [&](CoverScrapeResult value) { result = std::move(value); });
    REQUIRE(result);
    REQUIRE(result->errorCode == "cover_unavailable");
    REQUIRE(repo.getAnime(1)->coverUrl == "https://lain.bgm.tv/original.jpg");
}

TEST_CASE("consumer callback failure does not discard an already published cover") {
    TempData temp;
    SqliteDatabase db(temp.root / "test.db");
    db.migrate();
    SqliteMediaRepository repo(db);
    REQUIRE(sqlite3_exec(db.handle(), "INSERT INTO anime(display_title) VALUES('Show');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    repo.bindAnime(1, {123, "Show", "", "", "", 12, 2});
    FakeBangumi remote;
    auto bangumi = std::make_shared<BangumiService>(repo, remote);
    FakeImage image;
    auto scraper = std::make_shared<CoverScraper>(repo, bangumi, image, temp.root);
    int calls{};
    scraper->refresh(1, [&](CoverScrapeResult) {
        ++calls;
        throw std::runtime_error("consumer error");
    });
    REQUIRE(calls == 1);
    const auto coverUrl = repo.getAnime(1)->coverUrl;
    REQUIRE(coverUrl.starts_with("/api/covers/1/123/"));
    REQUIRE(scraper->readCached(1, 123, coverUrl.substr(coverUrl.rfind('/') + 1)));
}
