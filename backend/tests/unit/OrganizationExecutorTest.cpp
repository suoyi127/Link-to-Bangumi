#include "anime_vault/services/OrganizationExecutor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef ANIME_VAULT_STANDALONE_TEST_MAIN
#include <catch2/catch_session.hpp>
int main(int argc, char* argv[]) { return Catch::Session().run(argc, argv); }
#endif

namespace fs = std::filesystem;
using namespace anime_vault;

TEST_CASE("hardlink publication preserves source and never replaces a target") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-execute-" + std::to_string(fs::file_time_type::clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto sourceRoot = root / "source";
    const auto libraryRoot = root / "library";
    fs::create_directories(sourceRoot);
    fs::create_directories(libraryRoot);
    const auto source = fs::canonical(sourceRoot) / "Show 01.mkv";
    { std::ofstream output(source, std::ios::binary); output << "episode bytes"; }
    const auto target = fs::canonical(libraryRoot) / "Show" / "01.mkv";
    OrganizationExecutor executor(sourceRoot, libraryRoot);
    const ExecuteFileRequest request{source, target, fs::file_size(source), fs::last_write_time(source), "hardlink"};

    const auto result = executor.execute(request);
    CHECK(result.target == target);
    CHECK(result.bytes == fs::file_size(source));
    CHECK(fs::exists(source));
    CHECK(fs::exists(target));
    CHECK(fs::equivalent(source, target));
    { std::ifstream input(source, std::ios::binary); CHECK(std::string(std::istreambuf_iterator<char>(input), {}) == "episode bytes"); }

    const auto conflict = fs::canonical(libraryRoot) / "Show" / "02.mkv";
    { std::ofstream output(conflict, std::ios::binary); output << "existing bytes"; }
    auto conflictRequest = request;
    conflictRequest.target = conflict;
    try {
        (void)executor.execute(conflictRequest);
        FAIL("occupied target must reject publication");
    } catch (const OrganizationError& error) {
        CHECK(error.code() == "target_exists");
    }
    { std::ifstream input(conflict, std::ios::binary); CHECK(std::string(std::istreambuf_iterator<char>(input), {}) == "existing bytes"); }
    { std::ifstream input(source, std::ios::binary); CHECK(std::string(std::istreambuf_iterator<char>(input), {}) == "episode bytes"); }
}

TEST_CASE("copy publishes independent bytes and symlink reports a stable result") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-modes-" + std::to_string(fs::file_time_type::clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const auto sourceRoot = root / "source";
    const auto libraryRoot = root / "library";
    fs::create_directories(sourceRoot);
    fs::create_directories(libraryRoot);
    const auto source = fs::canonical(sourceRoot) / "Show 01.mkv";
    { std::ofstream output(source, std::ios::binary); output << "episode bytes"; }
    const auto modified = fs::last_write_time(source);
    OrganizationExecutor executor(sourceRoot, libraryRoot);
    const auto copyTarget = fs::canonical(libraryRoot) / "Show" / "01.mkv";
    REQUIRE(executor.execute({source, copyTarget, fs::file_size(source), modified, "copy"}).target == copyTarget);
    CHECK_FALSE(fs::equivalent(source, copyTarget));
    { std::ifstream input(copyTarget, std::ios::binary); CHECK(std::string(std::istreambuf_iterator<char>(input), {}) == "episode bytes"); }
    CHECK(fs::exists(source));
    CHECK(fs::last_write_time(source) == modified);

    const auto symlinkTarget = fs::canonical(libraryRoot) / "Show" / "02.mkv";
    try {
        const auto result = executor.execute({source, symlinkTarget, fs::file_size(source), modified, "symlink"});
        CHECK(result.target == symlinkTarget);
        CHECK(fs::is_symlink(symlinkTarget));
        CHECK(fs::equivalent(source, symlinkTarget));
    } catch (const OrganizationError& error) {
        CHECK(error.code() == "symlink_unavailable");
        CHECK_FALSE(fs::exists(symlinkTarget));
    }
    CHECK(fs::exists(source));
}
