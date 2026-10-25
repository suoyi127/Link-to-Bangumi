#include "anime_vault/services/GameService.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <regex>
namespace anime_vault {
namespace {
namespace fs = std::filesystem;
std::string utf8(const fs::path& path) { const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
fs::path fromUtf8(const std::string& value) { return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(value.data()), value.size())); }
fs::path checkedPath(const std::string& value, const LibraryFileAccess& files) {
    if (value.empty() || value.size() > 4096 || value.find('\0') != std::string::npos) throw GameError("invalid_game_path");
    auto cleaned = value;
    const auto trim = [&cleaned] {
        const auto begin = cleaned.find_first_not_of(" \t\r\n");
        cleaned = begin == std::string::npos ? "" : cleaned.substr(begin, cleaned.find_last_not_of(" \t\r\n") - begin + 1);
    };
    trim();
    // 复制路径时外层引号不属于文件名；只剥离成对包裹，保留路径内部字符。
    if (cleaned.size() >= 2 && cleaned.front() == '"' && cleaned.back() == '"') cleaned = cleaned.substr(1, cleaned.size() - 2);
    else if (cleaned.size() >= 6 && cleaned.starts_with("“") && cleaned.ends_with("”")) cleaned = cleaned.substr(3, cleaned.size() - 6);
    trim();
    const auto path = fromUtf8(cleaned);
    if (!files.isGameExecutableAccessible(path)) throw GameError("invalid_game_executable");
    std::error_code error; auto canonical = fs::canonical(path, error);
    if (error) throw GameError("invalid_game_path"); return canonical;
}
void validText(const std::string& value, std::size_t maximum, bool required = false) {
    if (value.size() > maximum || value.find('\0') != std::string::npos || (required && value.find_first_not_of(" \t\r\n") == std::string::npos)) throw GameError("invalid_game_metadata");
}
}
std::vector<GameResource> GameService::list() const {
    auto lock = repository_.lock();
    auto games = repository_.list();
    for (auto& game : games) game.missing = !files_.isGameExecutableAccessible(fromUtf8(game.path));
    return games;
}
GameResource GameService::get(std::int64_t id) const {
    for (const auto& game : list()) if (game.id == id) return game;
    throw GameError("game_not_found");
}
GameResource GameService::importExecutable(const fs::path& input) {
    const auto path = checkedPath(utf8(input), files_);
    auto title = utf8(path.stem());
    if (title == "game" || title == "Game" || title == "launcher" || title == "Launcher") title = utf8(path.parent_path().filename());
    validText(title, 600, true);
    auto lock = repository_.lock();
    return get(repository_.importExecutable(utf8(path), title));
}
void GameService::edit(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& value) {
    validText(title, 600, true); validText(developer, 600); validText(summary, 20000);
    auto lock = repository_.lock(); const auto previous = get(id);
    // 缺失时仍可编辑元数据；替换路径必须指向可访问 EXE。
    const auto path = value == previous.path ? previous.path : utf8(checkedPath(value, files_));
    if (repository_.pathUsedByOther(path, id)) throw GameError("game_path_already_imported");
    repository_.updateMetadata(id, title, developer, summary, path);
}
void GameService::removeUnavailable(std::int64_t id) {
    auto lock = repository_.lock();
    if (!get(id).missing) throw GameError("game_is_accessible");
    repository_.remove(id);
}
void GameService::launch(std::int64_t id, ProcessLauncher& launcher) const {
    const auto game = get(id);
    if (game.missing) throw GameError("game_executable_unavailable");
    const auto path = checkedPath(game.path, files_);
    if (!launcher.launchInDirectory(path, {}, path.parent_path())) throw GameError("game_launch_failed");
}
void GameService::bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    validText(title, 600, true); validText(developer, 600); validText(summary, 20000); validText(platform, 600);
    if (subjectId <= 0) throw GameError("invalid_subject_id");
    auto lock = repository_.lock(); get(id);
    repository_.bind(id, subjectId, title, developer, summary, platform);
}
void GameService::bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    if (!std::regex_match(vndbId, std::regex("^v[1-9][0-9]{0,9}$"))) throw GameError("invalid_vndb_id");
    validText(title, 600, true); validText(developer, 600); validText(summary, 20000); validText(platform, 600);
    auto lock = repository_.lock(); get(id);
    // 两个站点的 ID 独立保留，封面只属于当前选中的刮削来源。
    repository_.bindVndb(id, vndbId, title, developer, summary, platform);
}
bool GameService::setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime) {
    auto lock = repository_.lock(); return repository_.setCover(id, subjectId, bytes, mime);
}
bool GameService::setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime) {
    auto lock = repository_.lock(); return repository_.setVndbCover(id, vndbId, bytes, mime);
}
std::optional<CachedCover> GameService::cover(std::int64_t id) const {
    auto lock = repository_.lock(); return repository_.cover(id);
}
}
