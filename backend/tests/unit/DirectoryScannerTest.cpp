#include "anime_vault/services/DirectoryScanner.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using anime_vault::DirectoryScanner;

TEST_CASE("scanner returns only stable videos inside its source root") {
    const auto now = fs::file_time_type::clock::now();
    const auto fixture = fs::temp_directory_path() /
        ("anime-vault-scanner-" + std::to_string(now.time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code error; fs::remove_all(path, error); }
    } cleanup{fixture};
    const auto root = fixture / "root";
    fs::create_directories(root / "show");
    const auto video = root / "show" / "01.mkv";
    const auto pending = root / "show" / "01.part";
    const auto attachment = root / "show" / "01.srt";
    const auto recent = root / "show" / "02.mp4";
    const auto changed = root / "show" / "03.avi";
    const auto outside = fixture / "outside.mkv";
    for (const auto& file : {video, pending, attachment, recent, changed, outside}) {
        std::ofstream(file, std::ios::binary).put('x');
    }
    fs::last_write_time(video, now - std::chrono::hours(2));
    fs::last_write_time(outside, now - std::chrono::hours(2));
    fs::last_write_time(changed, now - std::chrono::hours(2));
    fs::last_write_time(recent, now);
    std::error_code linkError;
    fs::create_symlink(outside, root / "show" / "outside.mkv", linkError);
    linkError.clear();
    fs::create_symlink(video, root / "show" / "inside-alias.mkv", linkError);
    if (!linkError) REQUIRE(fs::is_symlink(root / "show" / "inside-alias.mkv"));

    DirectoryScanner scanner(root);
    REQUIRE(scanner.scan(std::chrono::hours(1), now).empty());
    std::ofstream(changed, std::ios::binary | std::ios::app).put('y');
    fs::last_write_time(changed, now - std::chrono::minutes(90));
    fs::last_write_time(recent, now + std::chrono::minutes(30));
    const auto files = scanner.scan(std::chrono::hours(1), now + std::chrono::hours(1));
    REQUIRE(files.size() == 1);
    CHECK(files.front().path == fs::canonical(video));
    CHECK(files.front().size == 1);
    CHECK(files.front().modifiedAt == fs::last_write_time(video));
}
