#include "anime_vault/services/NovelService.hpp"
#include "anime_vault/services/GameService.hpp"
#include "anime_vault/ports/NovelRepository.hpp"
#include "anime_vault/ports/GameRepository.hpp"
#include "anime_vault/ports/LibraryFileAccess.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <algorithm>
#include <fstream>
#include <functional>
#include <random>
#include <set>
using namespace anime_vault;
namespace fs = std::filesystem;
namespace {
struct DepthLock : RepositoryLock {
    int& depth;
    explicit DepthLock(int& value) : depth(value) { ++depth; }
    ~DepthLock() override { --depth; }
};
struct NovelFake : NovelRepository {
    mutable int depth{};
    std::vector<NovelWork> works;
    std::vector<std::int64_t> removed;
    bool removedWork{};
    ReaderConfig config;
    std::optional<NovelReadTarget> target;
    std::unique_ptr<RepositoryLock> lock() const override { return std::make_unique<DepthLock>(depth); }
    std::vector<NovelSource> listSources() const override { return {}; }
    std::vector<NovelWork> listWorksWithFiles() const override { REQUIRE(depth > 0); return works; }
    void removeFiles(const std::vector<std::int64_t>& ids) override {
        REQUIRE(depth > 0); removed.insert(removed.end(), ids.begin(), ids.end());
        for (auto& work : works) std::erase_if(work.files, [&](const auto& file) { return std::find(ids.begin(), ids.end(), file.id) != ids.end(); });
    }
    void removeWorkAndFiles(std::int64_t id) override { REQUIRE(depth > 0); removedWork = true; std::erase_if(works, [&](const auto& w) { return w.id == id; }); }
    NovelImportResult importSnapshot(const std::string&, bool, const std::vector<NovelImportEntry>&) override { return {}; }
    void updateWorkMetadata(std::int64_t, const std::string&, const std::string&, const std::string&) override {}
    bool updateFile(std::int64_t, std::int64_t, const std::string&) override { return true; }
    ReaderConfig reader() const override { REQUIRE(depth > 0); return config; }
    void saveReader(const ReaderConfig& value) override { config = value; }
    std::optional<NovelReadTarget> findReadTarget(std::int64_t) const override { REQUIRE(depth > 0); return target; }
    bool bind(std::int64_t, bool, std::int64_t, const std::string&, const std::string&, const std::string&) override { return true; }
    bool setCover(std::int64_t, bool, std::int64_t, const std::string&, const std::string&) override { return true; }
    std::optional<CachedCover> cover(std::int64_t, bool) const override { return {}; }
};
struct GameFake : GameRepository {
    mutable int depth{};
    std::vector<GameResource> games;
    bool removed{}, updated{}, duplicate{};
    std::string updatedPath;
    std::unique_ptr<RepositoryLock> lock() const override { return std::make_unique<DepthLock>(depth); }
    std::vector<GameResource> list() const override { REQUIRE(depth > 0); return games; }
    std::int64_t importExecutable(const std::string&, const std::string&) override { return 1; }
    bool pathUsedByOther(const std::string&, std::int64_t) const override { REQUIRE(depth > 0); return duplicate; }
    void updateMetadata(std::int64_t, const std::string&, const std::string&, const std::string&, const std::string& path) override { REQUIRE(depth > 0); updated = true; updatedPath = path; }
    void remove(std::int64_t) override { REQUIRE(depth > 0); removed = true; }
    void bind(std::int64_t, std::int64_t, const std::string&, const std::string&, const std::string&, const std::string&) override {}
    void bindVndb(std::int64_t, const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) override {}
    bool setCover(std::int64_t, std::int64_t, const std::string&, const std::string&) override { return true; }
    bool setVndbCover(std::int64_t, const std::string&, const std::string&, const std::string&) override { return true; }
    std::optional<CachedCover> cover(std::int64_t) const override { return {}; }
};
struct FilesFake : LibraryFileAccess {
    std::set<fs::path> available;
    mutable std::vector<fs::path> checked;
    std::function<void()> onCheck = [] {};
    bool safe{true};
    bool isLinked(const fs::path&) const override { return false; }
    bool isNovelPathSafe(const fs::path&) const override { onCheck(); return safe; }
    bool isNovelFileAccessible(const fs::path& path) const override { onCheck(); checked.push_back(path); return available.contains(path); }
    bool isGameExecutableAccessible(const fs::path& path) const override { onCheck(); checked.push_back(path); return available.contains(path); }
    bool isReaderExecutableValid(const std::string&) const override { return true; }
};
NovelWork book(std::int64_t id, std::initializer_list<NovelFile> files) { NovelWork w; w.id = id; w.files = files; return w; }
NovelFile volume(std::int64_t id, const std::string& path) { NovelFile f; f.id = id; f.path = path; return f; }
GameResource game(std::int64_t id, const std::string& path) { GameResource g; g.id = id; g.path = path; return g; }
std::string utf8(const fs::path& path) { auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
struct TempFiles {
    fs::path root = fs::temp_directory_path() / ("av-library-core-" + std::to_string(std::random_device{}()));
    TempFiles() { fs::create_directories(root); root = fs::canonical(root); }
    ~TempFiles() { std::error_code error; fs::remove_all(root, error); }
    fs::path make(const std::string& name) { auto path = root / name; std::ofstream(path) << "test"; return fs::canonical(path); }
};
struct Launcher : ProcessLauncher {
    fs::path executable, directory;
    std::vector<std::string> arguments;
    std::function<void()> onLaunch = [] {};
    bool launch(const fs::path& path, const std::vector<std::string>& args) override { onLaunch(); executable = path; arguments = args; return true; }
    bool launchInDirectory(const fs::path& path, const std::vector<std::string>& args, const fs::path& cwd) override { directory = cwd; return launch(path, args); }
};
}
TEST_CASE("Novel get checks and cleans other works under one persistence scope") {
    NovelFake repo; FilesFake files; NovelService service(repo, files);
    repo.works = {book(1, {volume(11, "good.txt")}), book(2, {volume(22, "gone.txt")})};
    files.available.insert("good.txt"); files.onCheck = [&] { REQUIRE(repo.depth > 0); };
    REQUIRE(service.get(1).files.size() == 1);
    REQUIRE(files.checked.size() == 2);
    REQUIRE(repo.removed == std::vector<std::int64_t>{22});
    REQUIRE(repo.works.size() == 2);
    REQUIRE(repo.works.back().files.empty()); REQUIRE(repo.depth == 0);
}
TEST_CASE("Novel deletion rechecks files and keeps cleanup when deletion is refused") {
    NovelFake repo; FilesFake files; NovelService service(repo, files);
    repo.works = {book(1, {volume(11, "good.txt"), volume(12, "gone.txt")})};
    files.available.insert("good.txt"); files.onCheck = [&] { REQUIRE(repo.depth > 0); };
    REQUIRE_THROWS_WITH(service.removeUnavailableWork(1), "novel_has_accessible_files");
    REQUIRE(repo.removed == std::vector<std::int64_t>{12}); REQUIRE_FALSE(repo.removedWork);
    files.available.clear(); service.removeUnavailableWork(1);
    REQUIRE(repo.removedWork); REQUIRE(repo.depth == 0);
}
TEST_CASE("Game get checks every record and deletion requires current unavailability") {
    GameFake repo; FilesFake files; GameService service(repo, files);
    repo.games = {game(1, "one.exe"), game(2, "two.exe")}; files.available.insert("one.exe");
    files.onCheck = [&] { REQUIRE(repo.depth > 0); };
    REQUIRE_FALSE(service.get(1).missing); REQUIRE(files.checked.size() == 2);
    REQUIRE_THROWS_WITH(service.removeUnavailable(1), "game_is_accessible"); REQUIRE_FALSE(repo.removed);
    service.removeUnavailable(2); REQUIRE(repo.removed); REQUIRE(repo.depth == 0);
}
TEST_CASE("Game metadata editing preserves missing paths and validates replacements") {
    TempFiles temp; auto replacement = temp.make("replacement.exe");
    GameFake repo; FilesFake files; GameService service(repo, files);
    repo.games = {game(1, "missing.exe")}; files.onCheck = [&] { REQUIRE(repo.depth > 0); };
    service.edit(1, "title", "developer", "summary", "missing.exe"); REQUIRE(repo.updatedPath == "missing.exe");
    repo.updated = false;
    REQUIRE_THROWS_WITH(service.edit(1, "title", "", "", utf8(replacement)), "invalid_game_executable"); REQUIRE_FALSE(repo.updated);
    files.available.insert(replacement); repo.duplicate = true;
    REQUIRE_THROWS_WITH(service.edit(1, "title", "", "", utf8(replacement)), "game_path_already_imported"); REQUIRE_FALSE(repo.updated);
    repo.duplicate = false; service.edit(1, "title", "", "", utf8(replacement)); REQUIRE(repo.updatedPath == utf8(replacement));
}
TEST_CASE("Library launch uses source containment and fixed arguments with original lock scopes") {
    TempFiles temp; auto file = temp.make("book.txt"); auto exe = temp.make("game.exe");
    NovelFake novels; FilesFake files; NovelService service(novels, files); Launcher launcher;
    novels.target = NovelReadTarget{utf8(file), utf8(temp.root), true}; novels.config = {"custom", utf8(exe)};
    files.onCheck = [&] { REQUIRE(novels.depth > 0); }; launcher.onLaunch = [&] { REQUIRE(novels.depth > 0); };
    REQUIRE_NOTHROW(service.read(1, launcher)); REQUIRE(launcher.arguments == std::vector<std::string>{utf8(file)});
    REQUIRE(files.checked.empty());
    novels.target->sourcePath = utf8(temp.root / "other");
    REQUIRE_THROWS_WITH(service.read(1, launcher), "novel_path_unsafe");
    GameFake games; games.games = {game(1, utf8(exe))}; files.available.insert(exe); GameService gameService(games, files);
    std::vector<int> depths; files.onCheck = [&] { depths.push_back(games.depth); }; launcher.onLaunch = [&] { REQUIRE(games.depth == 0); };
    gameService.launch(1, launcher);
    REQUIRE(depths.size() == 2); REQUIRE(depths.front() > 0); REQUIRE(depths.back() == 0);
    REQUIRE(launcher.executable == exe); REQUIRE(launcher.directory == exe.parent_path()); REQUIRE(launcher.arguments.empty());
}
