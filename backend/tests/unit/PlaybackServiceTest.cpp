#include "anime_vault/services/PlaybackService.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sqlite3.h>

namespace fs = std::filesystem;
using namespace anime_vault;

namespace {
std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
struct Fixture {
    fs::path root = fs::temp_directory_path() /
        ("anime-vault-playback-" + std::to_string(std::random_device{}()));
    fs::path source = root / "source", imported = root / "import", library = root / "library";
    fs::path mpv = root / "mpv.exe";
    std::unique_ptr<SqliteDatabase> db;
    std::unique_ptr<SqliteMediaRepository> repository;
    Fixture() {
        fs::create_directories(source);
        fs::create_directories(imported);
        fs::create_directories(library);
        std::ofstream(mpv) << "stub";
        db = std::make_unique<SqliteDatabase>(root / "vault.db");
        db->migrate();
        repository = std::make_unique<SqliteMediaRepository>(*db);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    std::int64_t add(const fs::path& path, const std::string& origin = "qb_download") {
        const auto scan = repository->createScan({0, utf8(path.parent_path()), "completed", 1, 1, 0, ""});
        MediaRecord media{0, scan, utf8(path), utf8(path.filename()), "1", "normal", 4, "inbox", 1};
        media.origin = origin;
        if (origin == "folder_import")
            media.folderImportId = repository->addFolderImport(utf8(fs::canonical(path.parent_path()))).id;
        return repository->insertMedia(media);
    }
};
struct RecordingLauncher final : ProcessLauncher {
    int calls{};
    fs::path executable;
    std::vector<std::string> arguments;
    bool launch(const fs::path& path, const std::vector<std::string>& args) override {
        ++calls; executable = path; arguments = args; return true;
    }
};
void requireCode(const std::function<void()>& action, const std::string& code) {
    try { action(); FAIL("expected playback error"); }
    catch (const PlaybackError& error) { REQUIRE(error.code() == code); }
}
}

TEST_CASE("playback chooses organized file and passes only fixed mpv options") {
    Fixture fixture;
    const auto source = fixture.source / "episode.mkv";
    const auto library = fixture.library / "show" / "episode.mkv";
    fs::create_directories(library.parent_path());
    std::ofstream(source) << "data";
    std::ofstream(library) << "data";
    const auto id = fixture.add(source);
    const auto sql = "UPDATE media_file SET library_path='" + utf8(library) + "',status='organized' WHERE id=" + std::to_string(id);
    REQUIRE(sqlite3_exec(fixture.db->handle(), sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    RecordingLauncher launcher;
    PlaybackService service(*fixture.repository, fixture.source, fixture.imported, fixture.library, launcher);
    service.play(id, utf8(fixture.mpv));
    REQUIRE(launcher.calls == 1);
    REQUIRE(launcher.executable == fs::canonical(fixture.mpv));
    REQUIRE(launcher.arguments == std::vector<std::string>{"--save-position-on-quit", "--", utf8(fs::canonical(library))});
}

TEST_CASE("playback rejects unknown, escaped, and missing files without launching") {
    Fixture fixture;
    const auto outside = fixture.root / "outside.mkv";
    std::ofstream(outside) << "data";
    const auto outsideId = fixture.add(outside);
    const auto missingId = fixture.add(fixture.source / "missing.mkv");
    RecordingLauncher launcher;
    PlaybackService service(*fixture.repository, fixture.source, fixture.imported, fixture.library, launcher);
    requireCode([&] { service.play(999999, utf8(fixture.mpv)); }, "media_not_found");
    requireCode([&] { service.play(outsideId, utf8(fixture.mpv)); }, "playback_path_outside_root");
    requireCode([&] { service.play(missingId, utf8(fixture.mpv)); }, "playback_file_missing");
    REQUIRE(launcher.calls == 0);
}

TEST_CASE("playback accepts a file within its registered folder import") {
    Fixture fixture;
    const auto folder = fixture.root / "selected";
    fs::create_directory(folder);
    const auto video = folder / "episode.mkv";
    { std::ofstream file(video); file << "data"; }
    const auto id = fixture.add(fs::canonical(video), "folder_import");
    RecordingLauncher launcher;
    PlaybackService service(*fixture.repository, fixture.source, fixture.imported, fixture.library, launcher);
    service.play(id, utf8(fixture.mpv));
    REQUIRE(launcher.calls == 1);
    REQUIRE(launcher.arguments.back() == utf8(fs::canonical(video)));
}

TEST_CASE("playback rejects a folder-import file outside its registered root") {
    Fixture fixture;
    const auto folder = fixture.root / "selected";
    fs::create_directory(folder);
    const auto outside = fixture.root / "outside.mkv";
    { std::ofstream file(outside); file << "data"; }
    const auto registered = fixture.repository->addFolderImport(utf8(fs::canonical(folder)));
    const auto scan = fixture.repository->createScan({0, "folder_import", "completed"});
    MediaRecord media{0, scan, utf8(fs::canonical(outside)), utf8(outside.filename()), "1", "normal", 4, "inbox", 1};
    media.origin = "folder_import";
    media.folderImportId = registered.id;
    const auto id = fixture.repository->insertMedia(media);
    RecordingLauncher launcher;
    PlaybackService service(*fixture.repository, fixture.source, fixture.imported, fixture.library, launcher);
    requireCode([&] { service.play(id, utf8(fixture.mpv)); }, "playback_path_outside_root");
    REQUIRE(launcher.calls == 0);
}
