#include "anime_vault/api/MediaService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

namespace fs = std::filesystem;
using namespace anime_vault;

namespace {
std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
struct Fixture {
    fs::path root = fs::temp_directory_path() /
        ("anime-vault-reconcile-" + std::to_string(std::random_device{}()));
    fs::path source = root / "source", imported = root / "import", library = root / "library";
    Fixture() { fs::create_directories(source); fs::create_directories(imported); fs::create_directories(library); }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    void video(const fs::path& path) {
        std::ofstream(path, std::ios::binary) << "video";
        fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(2));
    }
    void waitForRateLimit() { std::this_thread::sleep_for(std::chrono::milliseconds(1100)); }
};
}

TEST_CASE("successful source scan hides vanished file but keeps an observed unstable file") {
    Fixture fixture;
    const auto vanished = fixture.source / "Moonbound - 01.mkv";
    const auto pending = fixture.source / "Moonbound - 02.mkv";
    fixture.video(vanished);
    fixture.video(pending);
    SqliteDatabase db(fixture.root / "vault.db"); db.migrate();
    SqliteMediaRepository repo(db);
    anime_vault::api::MediaService service(repo, fixture.source, fixture.library, fixture.imported);
    REQUIRE(service.createScan(std::chrono::seconds{0}).status == "completed");
    const auto first = repo.listInbox();
    REQUIRE(first.size() == 2);
    const auto missingId = first.front().id;
    const auto animeId = *first.front().animeId;
    REQUIRE(fs::remove(vanished));
    fs::last_write_time(pending, fs::file_time_type::clock::now());
    fixture.waitForRateLimit();
    REQUIRE(service.createScan(std::chrono::seconds{60}).status == "completed");
    REQUIRE(repo.getMedia(missingId)->status == "missing");
    REQUIRE(repo.listInbox().size() == 1);
    REQUIRE(repo.getAnime(animeId)->media.size() == 1);
    fixture.video(vanished);
    fixture.waitForRateLimit();
    REQUIRE(service.createScan(std::chrono::seconds{0}).status == "completed");
    REQUIRE(repo.getMedia(missingId)->status == "inbox");
    REQUIRE(repo.listInbox().size() == 2);
}

TEST_CASE("source reconciliation does not hide imported or organized library media") {
    Fixture fixture;
    const auto organizedSource = fixture.source / "Show - 01.mkv";
    const auto importedSource = fixture.imported / "Other - 01.mkv";
    const auto organizedTarget = fixture.library / "Show - 01.mkv";
    fixture.video(organizedSource);
    fixture.video(importedSource);
    fixture.video(organizedTarget);
    SqliteDatabase db(fixture.root / "vault.db"); db.migrate();
    SqliteMediaRepository repo(db);
    anime_vault::api::MediaService service(repo, fixture.source, fixture.library, fixture.imported);
    REQUIRE(service.createScan(std::chrono::seconds{0}).status == "completed");
    fixture.waitForRateLimit();
    REQUIRE(service.createImportScan(std::chrono::seconds{0}).status == "completed");
    const auto sourceId = repo.listInbox().front().id;
    const auto importedId = repo.listInbox().back().id;
    const auto sql = "UPDATE media_file SET status='organized',library_path='" +
        utf8(organizedTarget) + "' WHERE id=" + std::to_string(sourceId);
    REQUIRE(sqlite3_exec(db.handle(), sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(fs::remove(organizedSource));
    fixture.waitForRateLimit();
    REQUIRE(service.createScan(std::chrono::seconds{0}).status == "completed");
    REQUIRE(repo.getMedia(sourceId)->status == "organized");
    REQUIRE(repo.getMedia(importedId)->status == "inbox");
}

TEST_CASE("library listing omits anime whose only file vanished but retains manually empty entries") {
    Fixture fixture;
    const auto file = fixture.source / "Gone - 01.mkv";
    fixture.video(file);
    SqliteDatabase db(fixture.root / "vault.db"); db.migrate();
    SqliteMediaRepository repo(db);
    anime_vault::api::MediaService service(repo, fixture.source, fixture.library, fixture.imported);
    REQUIRE(service.createScan(std::chrono::seconds{0}).status == "completed");
    REQUIRE(repo.listAnime().size() == 1);
    REQUIRE(sqlite3_exec(db.handle(),
        "INSERT INTO anime(display_title) VALUES('Manual empty')", nullptr, nullptr, nullptr) == SQLITE_OK);
    REQUIRE(fs::remove(file));
    fixture.waitForRateLimit();
    REQUIRE(service.createScan(std::chrono::seconds{0}).status == "completed");
    const auto visible = repo.listAnime();
    REQUIRE(visible.size() == 1);
    REQUIRE(visible.front().displayTitle == "Manual empty");
}
