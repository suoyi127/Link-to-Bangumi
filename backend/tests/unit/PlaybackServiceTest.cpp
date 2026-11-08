#include "anime_vault/services/MpvPlaybackSamples.hpp"
#include "anime_vault/services/MpvProgressMonitor.hpp"
#include "anime_vault/infrastructure/database/WatchProgressStore.hpp"
#include "anime_vault/services/PlaybackService.hpp"
#include "anime_vault/services/PlayerCatalog.hpp"
#include "anime_vault/infrastructure/database/SqliteDatabase.hpp"
#include "anime_vault/infrastructure/database/SqliteMediaRepository.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sqlite3.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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
    int defaultCalls{};
    fs::path executable;
    std::vector<std::string> arguments;
    bool launch(const fs::path& path, const std::vector<std::string>& args) override {
        ++calls; executable = path; arguments = args; return true;
    }
    bool openDefault(const fs::path& path) override { ++defaultCalls; executable = path; return true; }
};
void requireCode(const std::function<void()>& action, const std::string& code) {
    try { action(); FAIL("expected playback error"); }
    catch (const PlaybackError& error) { REQUIRE(error.code() == code); }
}
}

TEST_CASE("player selection uses fixed arguments and preserves default association boundaries") {
    Fixture fixture;
    const auto video = fixture.source / "episode.mkv";
    std::ofstream(video) << "data";
    const auto id = fixture.add(video);
    RecordingLauncher launcher;
    PlaybackService service(*fixture.repository, fixture.source, fixture.imported, fixture.library, launcher);
    service.play(id, utf8(fixture.mpv), "vlc");
    REQUIRE(launcher.arguments == std::vector<std::string>{utf8(fs::canonical(video))});
    service.play(id, "", "system");
    REQUIRE(launcher.defaultCalls == 1);
    requireCode([&] { service.play(id, utf8(fixture.mpv), "invalid"); }, "invalid_player");
    const auto script = fixture.source / "unsafe.cmd";
    std::ofstream(script) << "stub";
    requireCode([&] { service.play(fixture.add(script), "", "system"); }, "playback_format_unsupported");
    requireCode([&] { service.play(fixture.add(fixture.source / "missing.mkv"), "", "system"); }, "playback_file_missing");
    auto prefs = fixture.repository->getUiPreferences();
    prefs.playerType = "vlc"; prefs.playerExecutable = utf8(fixture.mpv);
    fixture.repository->putUiPreferences(prefs);
    REQUIRE(fixture.repository->getUiPreferences().playerType == "vlc");
    REQUIRE(fixture.repository->getUiPreferences().playerExecutable == utf8(fixture.mpv));
}

TEST_CASE("player catalog inherits quoted legacy mpv paths and marks missing files unavailable") {
    Fixture fixture;
    UiPreferences preferences;
    preferences.mpvExecutable = "\"" + utf8(fixture.mpv) + "\"";
    const auto options = discoverPlayers(preferences);
    REQUIRE(options.front().id == "mpv");
    REQUIRE(options.front().available);
    REQUIRE(options.front().executable == utf8(fixture.mpv));
    preferences.mpvExecutable = utf8(fixture.root / "missing.exe");
    REQUIRE_FALSE(discoverPlayers(preferences).front().available);
    REQUIRE_FALSE(validPlayerExecutable(utf8(fixture.source / "script.cmd")));
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


TEST_CASE("mpv keeps the last valid position and counts continuous playback across seeks and pauses") {
    MpvPlaybackSamples samples;
    samples.event(R"({"event":"property-change","name":"duration","data":100})", 0);
    samples.event(R"({"event":"property-change","name":"time-pos","data":2})", 0);
    samples.event(R"({"event":"property-change","name":"time-pos","data":3})", 1);
    samples.event(R"({"event":"seek"})", 1.1);
    samples.event(R"({"event":"property-change","name":"time-pos","data":70})", 1.2);
    samples.event(R"({"event":"property-change","name":"time-pos","data":71})", 2.2);
    samples.event(R"({"event":"property-change","name":"pause","data":true})", 2.3);
    REQUIRE(samples.flushRequested);
    samples.event(R"({"event":"property-change","name":"time-pos","data":72})", 4);
    samples.event(R"({"event":"property-change","name":"time-pos"})", 5);
    samples.event(R"({"event":"property-change","name":"duration","data":0})", 5);
    samples.event(R"({"event":"end-file"})", 6);
    REQUIRE(samples.finished);
    REQUIRE(samples.position == 72);
    REQUIRE(samples.duration == 100);
    REQUIRE(samples.ranges == std::vector<std::pair<double,double>>{{2,3},{70,71}});
}

TEST_CASE("mpv does not credit unannounced jumps or rewinds") {
    MpvPlaybackSamples samples;
    samples.event(R"({"event":"property-change","name":"time-pos","data":0})", 0);
    samples.event(R"({"event":"property-change","name":"time-pos","data":90})", 1);
    samples.event(R"({"event":"property-change","name":"time-pos","data":40})", 2);
    REQUIRE(samples.ranges.empty());
}

#ifdef _WIN32
TEST_CASE("mpv pipe reports the last valid position on pause and close and resumes only this file") {
    using namespace std::chrono_literals;
    Fixture fixture;
    // 使用临时目录与模拟播放器；测试不会启动桌面播放器。
    const auto video = fixture.source / "episode.mkv";
    std::ofstream(video) << "stub";
    const auto mediaId = fixture.add(video);
    WatchProgressStore store(*fixture.db);
    MpvProgressMonitor monitor(store);
    RecordingLauncher launcher;
    PlaybackService service(*fixture.repository, fixture.source, fixture.imported, fixture.library, launcher, &monitor);
    service.play(mediaId, utf8(fixture.mpv));
    REQUIRE(launcher.arguments[0] == "--resume-playback=yes");
    REQUIRE(launcher.arguments[1] == "--save-position-on-quit");
    REQUIRE(std::none_of(launcher.arguments.begin(), launcher.arguments.end(),
        [](const auto& option) { return option.starts_with("--start="); }));
    const auto session = store.begin(mediaId, false);
    store.sample(mediaId, session.id, 34, 100, {});
    service.play(mediaId, utf8(fixture.mpv));
    const auto options = launcher.arguments;
    REQUIRE(options[0] == "--no-resume-playback");
    REQUIRE(options[1] == "--start=34.000000");
    const std::string prefix = "--input-ipc-server=";
    REQUIRE(options[2].starts_with(prefix + R"(\\.\pipe\anime-vault-)"));
    struct Server {
        HANDLE handle;
        ~Server() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    } server{CreateNamedPipeA(options[2].substr(prefix.size()).c_str(), PIPE_ACCESS_DUPLEX,
         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
         1, 4096, 4096, 0, nullptr)};
    REQUIRE(server.handle != INVALID_HANDLE_VALUE);
    auto until = [](auto&& predicate) {
        auto deadline = std::chrono::steady_clock::now() + 4s;
        do {
            if (predicate()) return true;
            std::this_thread::sleep_for(20ms);
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    };
    REQUIRE(until([&] {
        return ConnectNamedPipe(server.handle, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    }));
    auto send = [&](const std::string& events) {
        DWORD written{};
        REQUIRE(WriteFile(server.handle, events.data(), static_cast<DWORD>(events.size()), &written, nullptr));
        REQUIRE(written == events.size());
    };
    auto path = options.back();
    std::replace(path.begin(), path.end(), '\\', '/');
    send("{\"event\":\"property-change\",\"name\":\"path\",\"data\":\"" + path + "\"}\n"
         R"({"event":"property-change","name":"duration","data":100})" "\n"
         R"({"event":"property-change","name":"time-pos","data":40})" "\n"
         R"({"event":"property-change","name":"pause","data":true})" "\n");
    REQUIRE(until([&] { return store.resumePosition(mediaId) == 40; }));
    send(R"({"event":"property-change","name":"time-pos","data":42})" "\n"
         R"({"event":"property-change","name":"time-pos","data":null})" "\n"
         R"({"event":"end-file"})" "\n");
    REQUIRE(until([&] { return store.resumePosition(mediaId) == 42; }));
    service.play(mediaId, utf8(fixture.mpv), "mpv", true);
    REQUIRE(launcher.arguments[1] == "--start=0.000000");
    REQUIRE(launcher.arguments[2] != options[2]);
}
#endif
