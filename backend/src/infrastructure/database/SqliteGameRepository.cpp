#include "anime_vault/infrastructure/database/SqliteGameRepository.hpp"
#include "SqliteRepositoryLock.hpp"
#include "SqliteLibrarySupport.hpp"
namespace anime_vault {
namespace {
using Statement = sqlite_support::Statement;
Statement prepare(sqlite3* db, const char* sql) { return sqlite_library_support::prepare<GameError>(db, sql, "game_storage_error"); }
std::string text(const Statement& statement, int column) { return sqlite_support::column(statement.get(), column); }
void bindText(const Statement& statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement.get(), index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw GameError("game_storage_error");
}
void done(const Statement& statement) { sqlite_library_support::done<GameError>(statement.get(), "game_storage_error"); }
}
std::unique_ptr<RepositoryLock> SqliteGameRepository::lock() const { return std::make_unique<SqliteRepositoryLock>(db_); }
std::vector<GameResource> SqliteGameRepository::list() const {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "SELECT id,path,title,developer,summary,platform,subject_id,manual_metadata,cover IS NOT NULL,vndb_id,metadata_source FROM game_resource ORDER BY title,id");
    std::vector<GameResource> result; int state{};
    while ((state = sqlite3_step(statement.get())) == SQLITE_ROW) {
        GameResource game; game.id = sqlite3_column_int64(statement.get(), 0); game.path = text(statement, 1); game.title = text(statement, 2);
        game.developer = text(statement, 3); game.summary = text(statement, 4); game.platform = text(statement, 5);
        if (sqlite3_column_type(statement.get(), 6) != SQLITE_NULL) game.subjectId = sqlite3_column_int64(statement.get(), 6);
        game.manualMetadata = sqlite3_column_int(statement.get(), 7) != 0; game.hasCover = sqlite3_column_int(statement.get(), 8) != 0;
        game.vndbId = text(statement, 9); game.metadataSource = text(statement, 10);
        result.push_back(std::move(game));
    }
    if (state != SQLITE_DONE) throw GameError("game_storage_error"); return result;
}
std::int64_t SqliteGameRepository::importExecutable(const std::string& path, const std::string& title) {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "INSERT INTO game_resource(path,title) VALUES(?,?) ON CONFLICT(path) DO NOTHING");
    bindText(statement, 1, path); bindText(statement, 2, title); done(statement);
    auto existing = prepare(db_.handle(), "SELECT id FROM game_resource WHERE path=?"); bindText(existing, 1, path);
    if (sqlite3_step(existing.get()) != SQLITE_ROW) throw GameError("game_storage_error");
    return sqlite3_column_int64(existing.get(), 0);
}
bool SqliteGameRepository::pathUsedByOther(const std::string& path, std::int64_t id) const {
    std::lock_guard lock(db_.mutex());
    auto duplicate = prepare(db_.handle(), "SELECT id FROM game_resource WHERE path=? AND id<>?");
    bindText(duplicate, 1, path); sqlite3_bind_int64(duplicate.get(), 2, id);
    return sqlite3_step(duplicate.get()) == SQLITE_ROW;
}
void SqliteGameRepository::updateMetadata(std::int64_t id, const std::string& title, const std::string& developer, const std::string& summary, const std::string& path) {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=?,developer=?,summary=?,path=?,manual_metadata=1 WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, path); sqlite3_bind_int64(statement.get(), 5, id); done(statement);
}
void SqliteGameRepository::remove(std::int64_t id) {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "DELETE FROM game_resource WHERE id=?"); sqlite3_bind_int64(statement.get(), 1, id); done(statement);
}
void SqliteGameRepository::bind(std::int64_t id, std::int64_t subjectId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,developer=CASE WHEN manual_metadata=0 THEN ? ELSE developer END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END,platform=?,cover=CASE WHEN subject_id=? AND metadata_source='bangumi' THEN cover ELSE NULL END,cover_mime=CASE WHEN subject_id=? AND metadata_source='bangumi' THEN cover_mime ELSE '' END,subject_id=?,metadata_source='bangumi' WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, platform);
    for (int index = 5; index <= 7; ++index) sqlite3_bind_int64(statement.get(), index, subjectId);
    sqlite3_bind_int64(statement.get(), 8, id); done(statement);
}
void SqliteGameRepository::bindVndb(std::int64_t id, const std::string& vndbId, const std::string& title, const std::string& developer, const std::string& summary, const std::string& platform) {
    std::lock_guard lock(db_.mutex());
    // 两个站点的 ID 独立保留，封面只属于当前选中的刮削来源。
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET title=CASE WHEN manual_metadata=0 THEN ? ELSE title END,developer=CASE WHEN manual_metadata=0 THEN ? ELSE developer END,summary=CASE WHEN manual_metadata=0 THEN ? ELSE summary END,platform=?,cover=CASE WHEN vndb_id=? AND metadata_source='vndb' THEN cover ELSE NULL END,cover_mime=CASE WHEN vndb_id=? AND metadata_source='vndb' THEN cover_mime ELSE '' END,vndb_id=?,metadata_source='vndb' WHERE id=?");
    bindText(statement, 1, title); bindText(statement, 2, developer); bindText(statement, 3, summary); bindText(statement, 4, platform);
    for (int index = 5; index <= 7; ++index) bindText(statement, index, vndbId);
    sqlite3_bind_int64(statement.get(), 8, id); done(statement);
}
bool SqliteGameRepository::setCover(std::int64_t id, std::int64_t subjectId, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex());
    auto statement = prepare(db_.handle(), "UPDATE game_resource SET cover=?,cover_mime=? WHERE id=? AND subject_id=? AND metadata_source='bangumi'");
    sqlite3_bind_blob(statement.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(statement, 2, mime);
    sqlite3_bind_int64(statement.get(), 3, id); sqlite3_bind_int64(statement.get(), 4, subjectId); done(statement); return sqlite3_changes(db_.handle()) != 0;
}
std::optional<CachedCover> SqliteGameRepository::cover(std::int64_t id) const {
    std::lock_guard lock(db_.mutex()); auto statement = prepare(db_.handle(), "SELECT cover,cover_mime FROM game_resource WHERE id=?"); sqlite3_bind_int64(statement.get(), 1, id);
    if (sqlite3_step(statement.get()) != SQLITE_ROW || sqlite3_column_type(statement.get(), 0) == SQLITE_NULL) return {};
    CachedCover result; result.bytes.assign(static_cast<const char*>(sqlite3_column_blob(statement.get(), 0)), sqlite3_column_bytes(statement.get(), 0)); result.mimeType = text(statement, 1); return result;
}
bool SqliteGameRepository::setVndbCover(std::int64_t id, const std::string& vndbId, const std::string& bytes, const std::string& mime) {
    std::lock_guard lock(db_.mutex()); auto statement = prepare(db_.handle(), "UPDATE game_resource SET cover=?,cover_mime=? WHERE id=? AND vndb_id=? AND metadata_source='vndb'");
    sqlite3_bind_blob(statement.get(), 1, bytes.data(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT); bindText(statement, 2, mime);
    sqlite3_bind_int64(statement.get(), 3, id); bindText(statement, 4, vndbId); done(statement); return sqlite3_changes(db_.handle()) != 0;
}
}
