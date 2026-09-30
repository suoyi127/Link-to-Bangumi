#include "anime_vault/services/NovelService.hpp"
#include "anime_vault/services/NovelBangumiService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <random>
#include <algorithm>
#include <sqlite3.h>

using namespace anime_vault;
namespace fs = std::filesystem;
struct NovelFixture {
    fs::path root = fs::temp_directory_path() / ("av-novel-" + std::to_string(std::random_device{}()));
    NovelFixture() { fs::create_directories(root / "books"); }
    ~NovelFixture() { std::error_code ignored; fs::remove_all(root, ignored); }
    void file(const std::string& name) { std::ofstream(root / "books" / name) << "book"; }
};
struct ReaderLauncher : ProcessLauncher {
    std::vector<std::string> args;
    bool launch(const fs::path&, const std::vector<std::string>& values) override { args = values; return true; }
    bool openDefault(const fs::path& file) override { args = {file.string()}; return true; }
};
TEST_CASE("Deleting a novel requires all files to be unavailable and keeps source files") {
    NovelFixture fixture; fixture.file("Book Vol.01.txt"); fixture.file("Book Vol.02.txt");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db);
    service.importPath(fixture.root / "books");
    const auto work = service.list().front();
    fs::remove(fixture.root / "books" / "Book Vol.01.txt");
    REQUIRE_THROWS_AS(service.removeUnavailableWork(work.id), NovelError);
    REQUIRE(fs::exists(fixture.root / "books" / "Book Vol.02.txt"));
    fs::rename(fixture.root / "books" / "Book Vol.02.txt", fixture.root / "saved.txt");
    service.removeUnavailableWork(work.id);
    REQUIRE(service.list().empty());
    sqlite3_stmt* remainingFiles = nullptr;
    REQUIRE(sqlite3_prepare_v2(db.handle(), "SELECT COUNT(*) FROM novel_file", -1, &remainingFiles, nullptr) == SQLITE_OK);
    const std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> remainingGuard(remainingFiles, sqlite3_finalize);
    REQUIRE(sqlite3_step(remainingFiles) == SQLITE_ROW);
    REQUIRE(sqlite3_column_int(remainingFiles, 0) == 0);
    REQUIRE(fs::exists(fixture.root / "saved.txt"));
    REQUIRE_THROWS_AS(service.removeUnavailableWork(work.id), NovelError);
}
TEST_CASE("Novel listing cleans unavailable volumes without removing the novel work") {
    NovelFixture fixture; fixture.file("Book.txt");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db);
    service.importPath(fixture.root / "books" / "Book.txt");
    const auto id = service.list().front().id;
    fs::remove(fixture.root / "books" / "Book.txt");
    REQUIRE(service.get(id).files.empty());
    fixture.file("Book.txt");
    service.importPath(fixture.root / "books" / "Book.txt");
    REQUIRE_FALSE(service.get(id).files.front().missing);
    ReaderLauncher launcher; service.read(service.get(id).files.front().id, launcher);
    REQUIRE(launcher.args.size() == 1);
    fs::remove(fixture.root / "books" / "Book.txt"); fs::remove(fixture.root / "books");
    REQUIRE(service.get(id).files.empty());
    REQUIRE(service.get(id).title == "Book");
}
TEST_CASE("Novel volume cleanup is independent of other books in the same root") {
    NovelFixture fixture;
    fixture.file("Story Vol.01.txt"); fixture.file("Story Vol.02.txt"); fixture.file("Other.txt");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db);
    service.importPath(fixture.root / "books");
    fs::remove(fixture.root / "books" / "Story Vol.02.txt");
    const auto works = service.list();
    REQUIRE(works.size() == 2);
    const auto story = std::find_if(works.begin(), works.end(), [](const auto& work) { return work.title == "Story"; });
    REQUIRE(story != works.end());
    REQUIRE(story->files.size() == 1);
    REQUIRE(story->files.front().label == "Story Vol.01");
    REQUIRE(fs::exists(fixture.root / "books" / "Other.txt"));
}
TEST_CASE("Novel import groups volumes without modifying source files") {
    NovelFixture fixture; fixture.file("Story Vol.01.epub"); fixture.file("Story Vol.02.txt"); fixture.file("ignored.mkv");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate();
    NovelService service(db);
    auto imported = service.importPath(fixture.root / "books");
    REQUIRE(imported.fileCount == 2);
    REQUIRE(service.list().size() == 1);
    const auto work = service.list().front();
    REQUIRE(work.title == "Story");
    REQUIRE(work.files.size() == 2);
    REQUIRE(service.importPath(fixture.root / "books").fileCount == 2);
    REQUIRE(service.list().front().files.size() == 2);
    REQUIRE(fs::exists(fixture.root / "books" / "Story Vol.01.epub"));
    fs::remove(fixture.root / "books" / "Story Vol.02.txt");
    service.importPath(fixture.root / "books");
    const auto files = service.list().front().files;
    REQUIRE(files.size() == 1);
    REQUIRE_FALSE(files.front().missing);
    ReaderLauncher launcher;
    const auto available = std::find_if(files.begin(), files.end(), [](const auto& f) { return !f.missing; });
    service.read(available->id, launcher);
    REQUIRE(launcher.args.size() == 1);
    REQUIRE_THROWS_AS(service.read(work.files.back().id, launcher), NovelError);
}
TEST_CASE("Novel file import preserves binding and manual metadata on rescan") {
    NovelFixture fixture; fixture.file("Book.pdf");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db);
    service.importPath(fixture.root / "books" / "Book.pdf");
    auto work = service.list().front();
    service.edit(work.id, "中文书名", "作者", "简介");
    service.importPath(fixture.root / "books" / "Book.pdf");
    REQUIRE(service.list().size() == 1);
    REQUIRE(service.list().front().title == "中文书名");
    REQUIRE_THROWS_AS(service.importPath(fixture.root), NovelError); // 重叠来源不能改变既有安全边界。
}
TEST_CASE("Novel Bangumi matching keeps manga and ambiguous books from auto binding") {
    auto novel = NovelBangumiService::parseSubject(R"({"id":1,"type":1,"platform":"小说","name":"Story","name_cn":"故事","infobox":[{"key":"作者","value":"Author"},{"key":"别名","value":[{"v":"Alias"}]}]})");
    REQUIRE(novel.author == "Author"); REQUIRE(novel.aliases.front() == "Alias");
    REQUIRE(NovelBangumiService::uniqueMatch("Alias", {novel}) == 1);
    auto duplicate = novel; duplicate.id = 2;
    REQUIRE_FALSE(NovelBangumiService::uniqueMatch("Story", {novel, duplicate}));
    novel.platform = "漫画";
    REQUIRE_FALSE(NovelBangumiService::uniqueMatch("Story", {novel}));
    REQUIRE_THROWS_AS(NovelBangumiService::parseSubject(R"({"id":1,"type":2,"name":"Story"})"), NovelError);
}

TEST_CASE("Novel import recognizes Chinese and bracketed volume markers") {
    NovelFixture fixture;
    const auto a = fixture.root / "books" / fs::path(u8"中文小说 第01卷.epub");
    const auto b = fixture.root / "books" / fs::path(u8"中文小说 (02).txt");
    std::ofstream(a) << "book"; std::ofstream(b) << "book";
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db);
    service.importPath(fixture.root / "books");
    REQUIRE(service.list().size() == 1);
}
struct BookTransport : BangumiTransport {
    std::string book = R"({"id":870,"type":1,"platform":"小说","series":true,"name":"Story","name_cn":"故事","images":{"large":"https://lain.bgm.tv/pic/cover/l/aa/bb/870_test.jpg"},"infobox":[{"key":"作者","value":"Author"}]})";
    std::vector<Completion> pending;
    bool defer{};
    void search(std::string, Completion completion) override { completion({}, "wrong_anime_route"); }
    void searchAliases(std::string, Completion completion) override { completion({}, "wrong_anime_route"); }
    void searchBooks(std::string, Completion completion) override { completion(Response{200, "{\"data\":[" + book + "]}"}, ""); }
    void subject(std::int64_t, Completion completion) override { if (defer) pending.push_back(std::move(completion)); else completion(Response{200, book}, ""); }
};
struct BookCovers : CoverImageFetcher {
    void fetch(std::string, Completion completion) override { completion(std::string("\xff\xd8\xff\xd9", 4), ""); }
};
TEST_CASE("Novel scraping binds a book and caches a checked cover independently from anime") {
    NovelFixture fixture; fixture.file("Story Vol.01.epub"); fixture.file("Story Vol.02.txt");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db); service.importPath(fixture.root / "books");
    BookTransport transport; BookCovers covers; auto scraper = std::make_shared<NovelBangumiService>(service, transport, covers);
    const auto id = service.list().front().id;
    NovelScrapeResult result; scraper->scrape(id, false, {}, [&](auto r) { result = r; });
    REQUIRE(result.bound); REQUIRE(result.coverUpdated);
    REQUIRE(service.get(id).title == "故事"); REQUIRE(service.get(id).author == "Author"); REQUIRE(service.cover(id, false));
    REQUIRE_FALSE(service.get(id).files.front().subjectId);
    service.edit(id, "人工标题", "人工作者", "人工简介");
    scraper->scrape(id, false, {}, [&](auto r) { result = r; });
    REQUIRE(service.get(id).title == "人工标题");
}
TEST_CASE("An older asynchronous novel binding cannot replace a newer requested binding") {
    NovelFixture fixture; fixture.file("Story.epub");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db); service.importPath(fixture.root / "books");
    BookTransport transport; transport.defer = true; BookCovers covers; auto scraper = std::make_shared<NovelBangumiService>(service, transport, covers);
    const auto id = service.list().front().id;
    scraper->scrape(id, false, 870, [](auto) {}); scraper->scrape(id, false, 871, [](auto) {});
    transport.pending[1](BangumiTransport::Response{200, R"({"id":871,"type":1,"platform":"小说","name":"New"})"}, "");
    transport.pending[0](BangumiTransport::Response{200, transport.book}, "");
    REQUIRE(service.get(id).subjectId == 871);
}
TEST_CASE("Imported novels scrape serially instead of starting one network thread per book") {
    NovelFixture fixture; fixture.file("Story.epub"); fixture.file("Other.epub");
    SqliteDatabase db(fixture.root / "test.db"); db.migrate(); NovelService service(db); service.importPath(fixture.root / "books");
    auto works = service.list(); for (const auto& w : works) service.edit(w.id, "Story", "", "");
    BookTransport transport; transport.defer = true; BookCovers covers; auto scraper = std::make_shared<NovelBangumiService>(service, transport, covers);
    scraper->queue({works[0].id, works[1].id});
    REQUIRE(transport.pending.size() == 1);
    auto first = transport.pending[0]; first(BangumiTransport::Response{200, transport.book}, "");
    REQUIRE(transport.pending.size() == 2);
    auto second = transport.pending[1]; second(BangumiTransport::Response{200, transport.book}, "");
    REQUIRE(service.get(works[0].id).subjectId == 870);
    REQUIRE(service.get(works[1].id).subjectId == 870);
}
