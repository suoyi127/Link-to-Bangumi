#include "anime_vault/services/TargetPlanner.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace anime_vault;

TEST_CASE("target preview names episodes and reports unsafe or occupied targets") {
    const auto root = fs::temp_directory_path() /
        ("anime-vault-target-" + std::to_string(fs::file_time_type::clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    fs::create_directories(root);
    fs::create_directories(root / "library");
    const auto source = root / "source.mkv";
    std::ofstream(source).put('x');
    TargetPlanner planner(root / "library");
    const SourceFile file{source, 1, fs::last_write_time(source)};
    const auto episode = EpisodeNumber::fromParts(1, std::nullopt);
    const auto plan = planner.preview(file, "Moonbound", EpisodeType::normal, episode);
    CHECK(plan.target == fs::canonical(root / "library") / "Moonbound" / "Moonbound [01].mkv");
    CHECK(plan.conflicts.empty());
    CHECK_FALSE(fs::exists(plan.target));
    fs::create_directories(plan.target.parent_path());
    std::ofstream(plan.target).put('x');
    CHECK_FALSE(planner.preview(file, "Moonbound", EpisodeType::normal, episode).conflicts.empty());
    CHECK_THROWS_AS(planner.preview(file, "../escape", EpisodeType::normal, episode), std::invalid_argument);
    CHECK_THROWS_AS(planner.preview(file, "CON", EpisodeType::normal, episode), std::invalid_argument);
    CHECK_THROWS_AS(planner.preview(file, "Moonbound. ", EpisodeType::normal, episode), std::invalid_argument);
    CHECK_THROWS_AS(planner.preview(file, "Moonbound", EpisodeType::normal, episode, "../v2"), std::invalid_argument);
    CHECK_THROWS_AS(planner.preview(SourceFile{root / "source.txt"}, "Moonbound",
                                           EpisodeType::normal, episode), std::invalid_argument);
    CHECK(planner.preview(file, "Moonbound", EpisodeType::normal,
                          EpisodeNumber::fromParts(1, 5), "v2").target.filename() == "Moonbound [01.5]-v2.mkv");
    CHECK(planner.preview(file, "Moonbound", EpisodeType::sp, episode).target.filename() == "Moonbound [SP01].mkv");
    CHECK(planner.preview(file, "Moonbound", EpisodeType::ova, episode).target.filename() == "Moonbound [OVA01].mkv");
    CHECK(planner.preview(file, "Moonbound", EpisodeType::ncop, episode).target.filename() == "Moonbound [NCOP01].mkv");
    CHECK(planner.preview(file, "Moonbound", EpisodeType::nced, episode).target.filename() == "Moonbound [NCED01].mkv");
    const auto outside = root / "outside";
    fs::create_directories(outside);
    std::error_code linkError;
    fs::create_directory_symlink(outside, root / "library" / "Escape", linkError);
    if (!linkError) {
        CHECK_THROWS_AS(planner.preview(file, "Escape", EpisodeType::normal, episode), std::invalid_argument);
    }
    std::ofstream(root / "library" / "FileParent").put('x');
    const auto fileParent = planner.preview(file, "FileParent", EpisodeType::normal, episode);
    CHECK(fileParent.conflicts == std::vector<std::string>{"parent_not_directory"});
    const auto missingOutside = root / "missing-outside";
    linkError.clear();
    fs::create_directory_symlink(missingOutside, root / "library" / "Dangling", linkError);
    if (!linkError) {
        CHECK_THROWS_AS(planner.preview(file, "Dangling", EpisodeType::normal, episode), std::invalid_argument);
    }
    fs::remove_all(root / "library");
    std::ofstream(root / "library").put('x');
    CHECK_THROWS_AS(planner.preview(file, "Moonbound", EpisodeType::normal, episode), std::invalid_argument);
    fs::remove(root / "library");
    linkError.clear();
    fs::create_directory_symlink(outside, root / "library", linkError);
    if (!linkError) {
        CHECK_THROWS_AS(planner.preview(file, "Moonbound", EpisodeType::normal, episode), std::invalid_argument);
    }
}
