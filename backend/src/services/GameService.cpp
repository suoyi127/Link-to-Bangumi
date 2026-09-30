#include "anime_vault/services/GameService.hpp"
#include "anime_vault/infrastructure/ProcessLauncher.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <regex>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace anime_vault {
namespace {
namespace fs = std::filesystem;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;
Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* statement{};
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) throw GameError("game_storage_error");
    return {statement, sqlite3_finalize};
}
std::string text(const Statement& statement, int column) {
    const auto value = sqlite3_column_text(statement.get(), column);
    return value ? reinterpret_cast<const char*>(value) : "";
}
void bindText(const Statement& statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement.get(), index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw GameError("game_storage_error");
}
void done(const Statement& statement) {
    if (sqlite3_step(statement.get()) != SQLITE_DONE) throw GameError("game_storage_error");
}
std::string utf8(const fs::path& path) { const auto value = path.u8string(); return {reinterpret_cast<const char*>(value.data()), value.size()}; }
fs::path fromUtf8(const std::string& value) { return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(value.data()), value.size())); }
bool accessible(const fs::path& path) {
    std::error_code error;
    if (!path.is_absolute() || !fs::is_regular_file(path, error) || error) return false;
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".exe") return false;
    // 游戏目录常依赖相对路径；拒绝重解析路径，启动时固定使用 EXE 所在目录。
    for (auto current = path; !current.empty(); current = current.parent_path()) {
        if (fs::is_symlink(fs::symlink_status(current, error)) || error) return false;
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
#endif
        if (current == current.root_path()) break;
    }
    std::ifstream file(path, std::ios::binary); char header[2]{};
    return static_cast<bool>(file.read(header, 2)) && header[0] == 'M' && header[1] == 'Z';
}
fs::path checkedPath(const std::string& value) {
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
    if (!accessible(path)) throw GameError("invalid_game_executable");
    std::error_code error; auto canonical = fs::canonical(path, error);
    if (error) throw GameError("invalid_game_path"); return canonical;
}
void validText(const std::string& value, std::size_t maximum, bool required = false) {
    if (value.size() > maximum || value.find('\0') != std::string::npos || (required && value.find_first_not_of(" \t\r\n") == std::string::npos)) throw GameError("invalid_game_metadata");
}
}
std::vector<GameResource> GameService::list() const {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "SELECT id,path,title,developer,summary,platform,subject_id,manual_metadata,cover IS NOT NULL,vndb_id,metadata_source FROM game_resource ORDER BY title,id");
    std::vector<GameResource> result; int state{};
    while ((state = sqlite3_step(statement.get())) == SQLITE_ROW) {
        GameResource game; game.id = sqlite3_column_int64(statement.get(), 0); game.path = text(statement, 1); game.title = text(statement, 2);
        game.developer = text(statement, 3); game.summary = text(statement, 4); game.platform = text(statement, 5);
        if (sqlite3_column_type(statement.get(), 6) != SQLITE_NULL) game.subjectId = sqlite3_column_int64(statement.get(), 6);
        game.manualMetadata = sqlite3_column_int(statement.get(), 7) != 0; game.hasCover = sqlite3_column_int(statement.get(), 8) != 0;
        game.vndbId = text(statement, 9); game.metadataSource = text(statement, 10);
        game.missing = !accessible(fromUtf8(game.path)); result.push_back(std::move(game));
    }
    if (state != SQLITE_DONE) throw GameError("game_storage_error"); return result;
}
GameResource GameService::get(std::int64_t id) const {
    for (const auto& game : list()) if (game.id == id) return game;
    throw GameError("game_not_found");
}
GameResource GameService::importExecutable(const fs::path& input) {
    const auto path = checkedPath(utf8(input));
    auto title = utf8(path.stem());
    if (title == "game" || title == "Game" || title == "launcher" || title == "Launcher") title = utf8(path.parent_path().filename());
    validText(title, 600, true);
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "INSERT INTO game_resource(path,title) VALUES(?,?) ON CONFLICT(path) DO NOTHING");
    bindText(statement, 1, utf8(path)); bindText(statement, 2, title); done(statement);
    auto existing = prepare(db_.handle(), "SELECT id FROM game_resource WHERE path=?"); bindText(existing, 1, utf8(path));
    if (sqlite3_step(existing.get()) != SQLITE_ROW) throw GameError("game_storage_error");
    return get(sqlite3_column_int64(existing.get(), 0));
}
void GameService::edit(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& value) {
    validText(title, 600, true); validText(developer, 600); validText(summary, 20000);
    std::lock_guard lock(db_.mutex()); const auto previous = get(id);
    // 缺失时仍可编辑元数据；替换路径必须指向可访问 EXE。
    const auto path = value == previous.path ? previous.path : utf8(checkedPath(value));
    auto duplicate = prepare(db_.handle(), "SELECT id FROM game_resource WHERE path=? AND id<>?");
    bindText(duplicate, 1, path); sqlite3_bind_int64(duplicate.get(), 2, id);
    if (sqlite3_step(duplicate.get()) == SQLITE_ROW) throw GameError("game_path_already_imported");
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=?,developer=?,summary=?,path=?,manual_metadata=1 WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, path); sqlite3_bind_int64(statement.get(), 5, id); done(statement);
}
void GameService::removeUnavailable(std::int64_t id) {
    std::lock_guard lock(db_.mutex());
    if (!get(id).missing) throw GameError("game_is_accessible");
    auto statement = prepare(db_.handle(), "DELETE FROM game_resource WHERE id=?"); sqlite3_bind_int64(statement.get(), 1, id); done(statement);
}
void GameService::launch(std::int64_t id, ProcessLauncher& launcher) const {
    const auto game = get(id);
    if (game.missing) throw GameError("game_executable_unavailable");
    const auto path = checkedPath(game.path);
    if (!launcher.launchInDirectory(path, {}, path.parent_path())) throw GameError("game_launch_failed");
}
void GameService::bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    validText(title, 600, true); validText(developer, 600); validText(summary, 20000); validText(platform, 600);
    if (subjectId <= 0) throw GameError("invalid_subject_id");
    std::lock_guard lock(db_.mutex()); get(id);
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,developer=CASE WHEN manual_metadata=0 THEN ? ELSE developer END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END,platform=?,cover=CASE WHEN subject_id=? AND metadata_source='bangumi' THEN cover ELSE NULL END,cover_mime=CASE WHEN subject_id=? AND metadata_source='bangumi' THEN cover_mime ELSE '' END,subject_id=?,metadata_source='bangumi' WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, platform);
    for (int index = 5; index <= 7; ++index) sqlite3_bind_int64(statement.get(), index, subjectId);
    sqlite3_bind_int64(statement.get(), 8, id); done(statement);
}
bool GameService::setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET cover=?,cover_mime=? WHERE id=? AND subject_id=? AND metadata_source='bangumi'");
    sqlite3_bind_blob(statement.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(statement, 2, mime);
    sqlite3_bind_int64(statement.get(), 3, id); sqlite3_bind_int64(statement.get(), 4, subjectId); done(statement); return sqlite3_changes(db_.handle()) != 0;
}
std::optional<CachedCover> GameService::cover(std::int64_t id) const {
    std::lock_guard lock(db_.mutex()); auto statement = prepare(db_.handle(), "SELECT cover,cover_mime FROM game_resource WHERE id=?"); sqlite3_bind_int64(statement.get(), 1, id);
    if (sqlite3_step(statement.get()) != SQLITE_ROW || sqlite3_column_type(statement.get(), 0) == SQLITE_NULL) return {};
    CachedCover result; result.bytes.assign(static_cast<const char*>(sqlite3_column_blob(statement.get(), 0)), sqlite3_column_bytes(statement.get(), 0)); result.mimeType = text(statement, 1); return result;
}
void GameService::bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    if (!std::regex_match(vndbId, std::regex("^v[1-9][0-9]{0,9}$"))) throw GameError("invalid_vndb_id");
    validText(title, 600, true); validText(developer, 600); validText(summary, 20000); validText(platform, 600);
    std::lock_guard lock(db_.mutex()); get(id);
    // 两个站点的 ID 独立保留，封面只属于当前选中的刮削来源。
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,developer=CASE WHEN manual_metadata=0 THEN ? ELSE developer END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END,platform=?,cover=CASE WHEN vndb_id=? AND metadata_source='vndb' THEN cover ELSE NULL END,cover_mime=CASE WHEN vndb_id=? AND metadata_source='vndb' THEN cover_mime ELSE '' END,vndb_id=?,metadata_source='vndb' WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, platform);
    for (int index = 5; index <= 7; ++index) bindText(statement, index, vndbId);
    sqlite3_bind_int64(statement.get(), 8, id); done(statement);
}
bool GameService::setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex()); auto statement = prepare(db_.handle(), "UPDATE game_resource SET cover=?,cover_mime=? WHERE id=? AND vndb_id=? AND metadata_source='vndb'");
    sqlite3_bind_blob(statement.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(statement, 2, mime);
    sqlite3_bind_int64(statement.get(), 3, id); bindText(statement, 4, vndbId); done(statement); return sqlite3_changes(db_.handle()) != 0;
}
}
